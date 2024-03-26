#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pshine/gfx.h>
#define PSHINE_INTERNAL_
#define PSHINE_MODNAME main
#include <pshine/mod.h>

// todo: implement atmospheric scattering,
//       a model renderer, pbr materials,
//       rocket parts, particles/exhaust,
//       physics, realistic aerodynamics.
// /j

// returns a malloc'd buffer
char *pshine_read_file(const char *fname, size_t *size);

typedef struct { float x, y; } float2;
typedef struct { float x, y, z; } float3;
typedef struct { float data[4][4]; } float4x4;

static inline float3 float3neg(float3 v) { return (float3){ -v.x, -v.y, -v.z }; }
static inline float float3dot(float3 a, float3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline float float3mag2(float3 v) { return float3dot(v, v); }
static inline float float3mag(float3 v) { return sqrtf(float3mag2(v)); }
static inline float3 float3div(float3 v, float s) { return (float3){ v.x/s, v.y/s, v.z/s }; }
static inline float3 float3mul(float3 v, float s) { return (float3){ v.x*s, v.y*s, v.z*s }; }
static inline float3 float3add(float3 a, float3 b) { return (float3){ a.x+b.x, a.y+b.y, a.z+b.z }; }
static inline float3 float3sub(float3 a, float3 b) { return (float3){ a.x-b.x, a.y-b.y, a.z-b.z }; }
static inline float3 float3norm(float3 v) {
	float m = float3mag2(v);
	if (fabsf(m) <= 0.00001f) return (float3){};
	return float3div(v, sqrtf(m));
}

static inline void setfloat4x4iden(float4x4 *m) {
	memset(m->data, 0, sizeof(m->data));
	m->data[0][0] = 1.0f;
	m->data[1][1] = 1.0f;
	m->data[2][2] = 1.0f;
	m->data[3][3] = 1.0f;
}

static inline void floata4mula(float o[4], const float v[4], float s) {
	o[0] += v[0] * s;
	o[1] += v[1] * s;
	o[2] += v[2] * s;
	o[3] += v[3] * s;
}

static inline void float4x4trans(float4x4 *m, float3 d) {
	float r[4] = {};
	floata4mula(r, m->data[0], d.x);
	floata4mula(r, m->data[1], d.y);
	floata4mula(r, m->data[2], d.z);
	floata4mula(m->data[3], r, 1);
}

static inline void float4x4scale(float4x4 *m, float3 s) {
	floata4mula(m->data[0], m->data[0], s.x);
	floata4mula(m->data[1], m->data[1], s.y);
	floata4mula(m->data[2], m->data[2], s.z);
}

static inline void setfloat4x4persp(float4x4 *m, float fov, float aspect, float znear, float zfar) {
	// http://www.songho.ca/opengl/gl_projectionmatrix.html#perspective
	const float π = 1.618033f;
	memset(m->data, 0, sizeof(m->data));
	float tg = tanf(fov * 0.5f * π / 180.0f); // tangent of half fovY
	float top = znear * tg; // half height of near plane
	float right = top * aspect; // half width of near plane

	m->data[0][0] = znear / right;
	m->data[1][1] = znear / top;
	m->data[2][2] = -(zfar + znear) / (zfar - znear);
	m->data[2][3] = -1.0f;
	m->data[3][2] = -(2.0f * zfar * znear) / (zfar - znear);
}

static inline void float4x4mul(float4x4 *res, const float4x4 *m1, const float4x4 *m2) {
	for (size_t i = 0; i < 4; ++i) {
		for (size_t j = 0; j < 4; ++j) {
			res->data[j][i] = 0;
			for (size_t k = 0; k < 4; ++k)
				res->data[j][i] += m1->data[k][i] * m2->data[j][k];
		}
	}
}

struct uniforms {
	float4x4 mvp;
	float3 sun_dir;
	float3 sun_col;
};

struct vertex {
	float3 pos;
	float3 nor;
	float2 tex;
};

enum : size_t { vertex_nattr = 3 };
static const struct gfx_attr vertex_attrs[vertex_nattr] = {
	{
		.type = GFX_FLOAT32,
		.index = 0,
		.count = 3,
		.offset = offsetof(struct vertex, pos)
	},
	{
		.type = GFX_FLOAT32,
		.index = 1,
		.count = 3,
		.offset = offsetof(struct vertex, nor)
	},
	{
		.type = GFX_FLOAT32,
		.index = 2,
		.count = 2,
		.offset = offsetof(struct vertex, tex)
	}
};

struct mesh_data {
	size_t vertex_count, index_count;
	struct vertex *vertices;
	uint32_t *indices;
};

float lerpf(float a, float b, float t) {
	return a * (1 - t) + b * t;
}

float3 lerpf3(float3 a, float3 b, float t) {
	return float3add(float3mul(a, 1 - t), float3mul(b, t));
}

// requires: |a| = |b|
float3 spheregen_lerpf3(float3 a, float3 b, float t) {
	float m2 = float3mag2(a);
	if (fabsf(m2) < 0.00001f) return lerpf3(a, b, t);
	float ϕ = acosf(float3dot(a, b) / m2);
	float isinϕ = 1.0f / sinf(ϕ);
	float
		ca = sinf((1 - t) * ϕ) * isinϕ,
		cb = sinf(t * ϕ) * isinϕ;
	return float3add(float3mul(a, ca), float3mul(b, cb));
}

struct vertex spheregen_lerpvtx(
	const struct vertex *a,
	const struct vertex *b,
	float t
) {
	float3 pos = spheregen_lerpf3(a->pos, b->pos, t);
	float3 nor = spheregen_lerpf3(a->nor, b->nor, t);
	return (struct vertex){ pos, float3norm(nor), {} };
}

void write_wavefront(
	const struct mesh_data *m,
	const char *fname
) {
	FILE *fout = fopen(fname, "w");
	for (size_t i = 0; i < m->vertex_count; ++i) {
		struct vertex v = m->vertices[i];
		fprintf(fout, "v %f %f %f\n", v.pos.x, v.pos.y, v.pos.z);
	}
	for (size_t i = 0; i < m->index_count - 2; i += 3) {
		uint32_t *xs = m->indices + i;
		fprintf(fout, "f %u %u %u\n", xs[0] + 1, xs[1] + 1, xs[2] + 1);
	}
	fclose(fout);
}

void generate_sphere_mesh(size_t n, struct mesh_data *data) {
	// octahedron -> subdiv tris (each edge). instead of lerp, use slerp
	data->index_count = n * n * 3 * 8; // subdiv tri = n^2 tris, 3 idx/tri, 8 tri/faces
	data->indices = calloc(data->index_count, sizeof(*data->indices));
	data->vertex_count = 4 * (n * n - 1) + 6;
	data->vertices = calloc(data->vertex_count, sizeof(*data->vertices));

	size_t nvtx = 0, nidx = 0;
	
	{
		struct vertex vtxs[6] = {
			{ { +.5,   0,   0 }, { +1,  0,  0 } },
			{ {   0,   0, +.5 }, {  0,  0, +1 } },
			{ { -.5,   0,   0 }, { -1,  0,  0 } },
			{ {   0,   0, -.5 }, {  0,  0, -1 } },
			{ {   0, +.5,   0 }, {  0, +1,  0 } },
			{ {   0, -.5,   0 }, {  0, -1,  0 } },
		};
		memcpy(data->vertices, vtxs, sizeof(vtxs));
		nvtx += 6;
	}

	struct strip {
		uint32_t l, m, r;
	} strips[8][n + 1]; // all except the vertex.

	// set the 2 top/bottom single-vertex strips
	for (uint32_t i = 0; i < 2; ++i) {
		uint32_t a = 4 + i;
		for (uint32_t b = 0; b < 4; ++b) {
			strips[4 * i + b][0] = (struct strip){
				a, a, a
			};
		}
	}
	
	// generate the 4 shared horiz. strips
	for (uint32_t i = 0; i < 4; ++i) {
		struct vertex *va = &data->vertices[i], *vb = &data->vertices[(i + 1) % 4];

		float dt = 1.0f / n;

		strips[i][n] = strips[4 + i][n] = (struct strip){
			i, nvtx, (i + 1) % 4
		};

		for (uint32_t j = 1; j < n; ++j) {
			PSHINE_CHECK(nvtx < data->vertex_count, "out of range vtx");
			data->vertices[nvtx] = spheregen_lerpvtx(va, vb, dt * j);
			++nvtx;
		}
	}

	// generate the vert. left+right pairs for each face/edge
	for (uint32_t i = 0; i < 2; ++i) {
		uint32_t a = 4 + i;
		size_t nvtx0 = nvtx;
		for (uint32_t b = 0; b < 4; ++b) {
			uint32_t face = i * 4 + b;
			struct vertex *va = &data->vertices[a], *vb = &data->vertices[b];
			float dt = 1.0f / n;
			// all except the last because its a shared
			// horizontal strip that we already generated.
			for (uint32_t j = 1; j < n; ++j) {
				PSHINE_CHECK(nvtx < data->vertex_count, "out of range vtx");
				data->vertices[nvtx] = spheregen_lerpvtx(va, vb, dt * j);
				strips[face][j].l = nvtx;
				strips[face][j].r = nvtx0 + ((b + 1) % 4) * (n - 1) + (j - 1);
				nvtx += 1;
			}
		}
	}

	// generate the rest of the strips
	for (uint32_t i = 0; i < 2; ++i) {
		uint32_t a = 4 + i;
		for (uint32_t b = 0; b < 4; ++b) {
			uint32_t face = i * 4 + b;
			for (uint32_t j = 1; j < n; ++j) {
				strips[face][j].m = nvtx;
				uint32_t l = strips[face][j].l, r = strips[face][j].r;
				struct vertex *vl = &data->vertices[l], *vr = &data->vertices[r];
				float dt = 1.0f / j;
				for (uint32_t k = 1; k < j; ++k) {
					PSHINE_CHECK(nvtx < data->vertex_count, "out of range vtx");
					data->vertices[nvtx] = spheregen_lerpvtx(vl, vr, dt * k);
					++nvtx;
				}
			}
		}
	}

	// generate the triangles
	for (uint32_t i = 0; i < 2; ++i) {
		for (uint32_t b = 0; b < 4; ++b) {
			uint32_t face = i * 4 + b;
			for (uint32_t j = 0; j < n; ++j) {
				uint32_t p1 = strips[face][j + 1].l, p2 = strips[face][j].l;
				for (uint32_t k = 0; k < 2 * j + 1; ++k) {
					struct strip s = k % 2 == 0 ? strips[face][j + 1] : strips[face][j];
					uint32_t p3 = k + k % 2 == 2 * j ? s.r : s.m + k / 2;
					data->indices[nidx++] = p1;
					data->indices[nidx++] = p2;
					data->indices[nidx++] = p3;
					p1 = p2;
					p2 = p3;
				}
			}
		}
	}
}

struct planet {
	float radius;
	float atmo_radius;
};

struct game {
	struct planet p;
	gfx_instance instance;
	gfx_mesh sphere;
	gfx_uniform_buffer ubo;
	gfx_shader shader;
	gfx_render_pass render_pass;
	float3 cam_pos;
};

void init_game(struct game *g) {
	g->instance = gfx_create_instance(&(struct gfx_instance_info){
		.name = "planetshine",
		.user_ptr = g,
		.width = 800,
		.height = 600
	});
	const size_t sphere_mesh_samples = 3;
	{
		struct mesh_data mdata = {};
		generate_sphere_mesh(20, &mdata);
		struct vertex vtxs_quad[4] = {
			{ { -0.7f, -0.7f, -5.f } },
			{ { +0.7f, -0.7f, -5.f } },
			{ { -0.7f, +0.7f, -5.f } },
			{ { +0.7f, +0.7f, -5.f } },
		};
		write_wavefront(&mdata, "test.obj");
		g->sphere = gfx_create_mesh(
			g->instance,
			&(struct gfx_mesh_info){
				.attrs = vertex_attrs,
				.attr_count = vertex_nattr,
				.vertex_size = sizeof(struct vertex),
				.vertex_count = mdata.vertex_count,
				.vertex_data = mdata.vertices,
				.has_indices = GFX_TRUE,
				.index_type = GFX_INDEX_UINT32,
				.index_count = mdata.index_count,
				.index_data = mdata.indices
			}
		);
		free(mdata.vertices);
		free(mdata.indices);
	}
	{
		char *vert_src = pshine_read_file("data/surf.vert", NULL);
		char *frag_src = pshine_read_file("data/surf.frag", NULL);
		g->shader = gfx_create_shader(g->instance, &(struct gfx_shader_info){
			.vertex_source = vert_src,
			.fragment_source = frag_src,
			.depth = GFX_TRUE
		});
		free(vert_src);
		free(frag_src);
	}
	{
		g->ubo = gfx_create_uniform_buffer(g->instance, &(struct gfx_uniform_buffer_info){
			.memory = GFX_UNIFORM_BUFFER_WRITE_ONLY,
			.size = sizeof(struct uniforms)
		});
	}
	{
		g->render_pass = gfx_create_render_pass(g->instance, &(struct gfx_render_pass_info){
			.target = gfx_get_output_framebuffer(g->instance)
		});
	}
	g->cam_pos = (float3){ 10.0f, 20.0f, 100.0f };
}

void deinit_game(struct game *g) {
	gfx_destroy_shader(g->instance, g->shader);
	gfx_destroy_mesh(g->instance, g->sphere);
	gfx_destroy_uniform_buffer(g->instance, g->ubo);
	gfx_destroy_render_pass(g->instance, g->render_pass);
	gfx_destroy_instance(g->instance);
}

enum gfx_control_flow loop(
	gfx_instance instance,
	gfx_command_stream command_stream,
	float delta_time,
	void *user_ptr
) {
	struct game *g = user_ptr;

	struct uniforms uniforms = {};
	float aspect = gfx_get_width(instance) /(float) gfx_get_height(instance);

	{
		float3 delta_pos = {};
		if (gfx_is_key_down(instance, GFX_KEY_D)) delta_pos.x += 1.0f;
		else if (gfx_is_key_down(instance, GFX_KEY_A)) delta_pos.x -= 1.0f;
		if (gfx_is_key_down(instance, GFX_KEY_W)) delta_pos.z -= 1.0f;
		else if (gfx_is_key_down(instance, GFX_KEY_S)) delta_pos.z += 1.0f;
		if (gfx_is_key_down(instance, GFX_KEY_SPACE)) delta_pos.y += 1.0f;
		else if (gfx_is_key_down(instance, GFX_KEY_LEFT_SHIFT)) delta_pos.y -= 1.0f;
		const float speed = 10.0f;
		g->cam_pos = float3add(g->cam_pos, float3mul(float3norm(delta_pos), speed * delta_time));
	}

	float4x4 modelview = {};
	setfloat4x4iden(&modelview);
	float4x4trans(&modelview, float3neg(g->cam_pos));
	float4x4scale(&modelview, (float3){ 100, 100, 100 });
	float4x4 projection = {};
	setfloat4x4persp(&projection, 90.0f, aspect, 0.01f, 1000.0f);
	float4x4mul(&uniforms.mvp, &projection, &modelview);
	
	gfx_write_uniform_buffer(g->instance, g->ubo, &uniforms);
	
	gfx_cmd_begin_render_pass(
		instance,
		command_stream,
		g->render_pass,
		&(struct gfx_begin_render_pass_info){
			.color = (struct gfx_color){ 0, 0, 0, 1 },
			.depth_stencil = { .depth = 1.0f, .stencil = 0 }
		}
	);

	gfx_cmd_bind_shader(instance, command_stream, g->shader);
	gfx_cmd_bind_uniform_buffer(g->instance, command_stream, g->ubo, 0);
	gfx_cmd_draw(instance, command_stream, g->sphere);

	gfx_cmd_end_render_pass(instance, command_stream);

	return GFX_CONTROL_CONTINUE;
}

int main() {
	struct game game;
	init_game(&game);
	gfx_loop(game.instance, &loop);
	deinit_game(&game);
}
