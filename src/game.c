#include <pshine/game.h>
#include <string.h>
#include "math.h"
#include <pshine/util.h>

typedef struct pshine_static_mesh_vertex planet_vertex;

// requires: |a| = |b|
float3 spheregen_float3lerp(float3 a, float3 b, float t) {
	float m2 = float3mag2(a);
	if (fabsf(m2) < 0.00001f) return float3lerp(a, b, t);
	float ϕ = acosf(float3dot(a, b) / m2);
	float isinϕ = 1.0f / sinf(ϕ);
	float
		ca = sinf((1 - t) * ϕ) * isinϕ,
		cb = sinf(t * ϕ) * isinϕ;
	return float3add(float3mul(a, ca), float3mul(b, cb));
}

planet_vertex spheregen_vtxlerp(
	const planet_vertex *a,
	const planet_vertex *b,
	float t
) {
	float3 pos = spheregen_float3lerp(float3vs(a->position), float3vs(b->position), t);
	float3 nor = spheregen_float3lerp(float3vs(a->normal), float3vs(b->normal), t);
	nor = float3norm(nor);
	return (planet_vertex){
		{ pos.x, pos.y, pos.z },
		{ nor.x, nor.y, nor.z },
		{ 0.0f, 0.0f },
	};
}

void generate_sphere_mesh(size_t n, struct pshine_mesh_data *m) {
	// octahedron -> subdiv tris (each edge). instead of lerp, use slerp
	m->index_count = n * n * 3 * 8; // subdiv tri = n^2 tris, 3 idx/tri, 8 tri/faces
	m->indices = calloc(m->index_count, sizeof(uint32_t));
	m->vertex_count = 4 * (n * n - 1) + 6;
	m->vertices = calloc(m->vertex_count, sizeof(planet_vertex));
	planet_vertex *vertices = m->vertices; // for easier access.

	size_t nvtx = 0, nidx = 0;
	
	{
		planet_vertex vtxs[6] = {
			{ { +1,  0,  0 }, { +1,  0,  0 } },
			{ {  0,  0, +1 }, {  0,  0, +1 } },
			{ { -1,  0,  0 }, { -1,  0,  0 } },
			{ {  0,  0, -1 }, {  0,  0, -1 } },
			{ {  0, +1,  0 }, {  0, +1,  0 } },
			{ {  0, -1,  0 }, {  0, -1,  0 } },
		};
		memcpy(vertices + nvtx, vtxs, sizeof(vtxs));
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
		const planet_vertex *va = &vertices[i], *vb = &vertices[(i + 1) % 4];

		float dt = 1.0f / n;

		strips[i][n] = strips[4 + i][n] = (struct strip){
			i, nvtx, (i + 1) % 4
		};

		for (uint32_t j = 1; j < n; ++j) {
			PSHINE_CHECK(nvtx < m->vertex_count, "out of range vtx");
			vertices[nvtx] = spheregen_vtxlerp(va, vb, dt * j);
			++nvtx;
		}
	}

	// generate the vert. left+right pairs for each face/edge
	for (uint32_t i = 0; i < 2; ++i) {
		uint32_t a = 4 + i;
		size_t nvtx0 = nvtx;
		for (uint32_t b = 0; b < 4; ++b) {
			uint32_t face = i * 4 + b;
			planet_vertex *va = &vertices[a], *vb = &vertices[b];
			float dt = 1.0f / n;
			// all except the last because its a shared
			// horizontal strip that we already generated.
			for (uint32_t j = 1; j < n; ++j) {
				PSHINE_CHECK(nvtx < m->vertex_count, "out of range vtx");
				vertices[nvtx] = spheregen_vtxlerp(va, vb, dt * j);
				strips[face][j].l = nvtx;
				strips[face][j].r = nvtx0 + ((b + 1) % 4) * (n - 1) + (j - 1);
				nvtx += 1;
			}
		}
	}

	// generate the rest of the strips
	for (uint32_t i = 0; i < 2; ++i) {
		for (uint32_t b = 0; b < 4; ++b) {
			uint32_t face = i * 4 + b;
			for (uint32_t j = 1; j < n; ++j) {
				strips[face][j].m = nvtx;
				uint32_t l = strips[face][j].l, r = strips[face][j].r;
				planet_vertex *vl = &vertices[l], *vr = &vertices[r];
				float dt = 1.0f / j;
				for (uint32_t k = 1; k < j; ++k) {
					PSHINE_CHECK(nvtx < m->vertex_count, "out of range vtx");
					vertices[nvtx] = spheregen_vtxlerp(vl, vr, dt * k);
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
					m->indices[nidx++] = p1;
					m->indices[nidx++] = p2;
					m->indices[nidx++] = p3;
					p1 = p2;
					p2 = p3;
				}
			}
		}
	}
}

void pshine_generate_planet_mesh(const struct pshine_planet *planet, struct pshine_mesh_data *out_mesh) {
	generate_sphere_mesh(20, out_mesh);
}

static void init_planet(struct pshine_planet *planet) {
	planet->as_body.type = PSHINE_CELESTIAL_BODY_PLANET;
	planet->as_body.radius = 100.0f;
	planet->as_body.parent_ref = NULL;
	planet->as_body.position = (struct pshine_position3d){ 0.0f, 0.0f, 0.0f };
}

static void deinit_planet(struct pshine_planet *planet) {
	(void)planet;
}

void pshine_init_game(struct pshine_game *game) {
	game->celestial_body_count = 1;
	game->celestial_bodies = calloc(game->celestial_body_count, sizeof(struct pshine_celestial_body*));
	game->celestial_bodies[0] = calloc(1, sizeof(struct pshine_planet));
	init_planet((void*)game->celestial_bodies[0]);
}

void pshine_deinit_game(struct pshine_game *game) {
	for (size_t i = 0; i < game->celestial_body_count; ++i) {
		struct pshine_celestial_body *b = game->celestial_bodies[i];
		if (b == NULL) {
			PSHINE_WARN("null celestial body");
			continue;
		}
		switch (b->type) {
			case PSHINE_CELESTIAL_BODY_PLANET: deinit_planet((void*)b); break;
			default: PSHINE_PANIC("bad b->type: %d", (int)b->type); break;
		}
		free(b);
	}
	free(game->celestial_bodies);
}

void pshine_update_game(struct pshine_game *game, float delta_time) {
	float3 delta_pos = {};
	if (pshine_is_key_down(game->renderer, PSHINE_KEY_D)) delta_pos.x += 1.0f;
	else if (pshine_is_key_down(game->renderer, PSHINE_KEY_A)) delta_pos.x -= 1.0f;
	if (pshine_is_key_down(game->renderer, PSHINE_KEY_W)) delta_pos.z += 1.0f;
	else if (pshine_is_key_down(game->renderer, PSHINE_KEY_S)) delta_pos.z -= 1.0f;
	if (pshine_is_key_down(game->renderer, PSHINE_KEY_SPACE)) delta_pos.y += 1.0f;
	else if (pshine_is_key_down(game->renderer, PSHINE_KEY_LEFT_SHIFT)) delta_pos.y -= 1.0f;
	const float speed = 100.0f;
	float3 cam_pos = float3vs(&game->camera_position.x);
	*(float3*)(&game->camera_position.x) = float3add(cam_pos, float3mul(float3norm(delta_pos), speed * delta_time));
}
