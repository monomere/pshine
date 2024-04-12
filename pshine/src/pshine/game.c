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

static void init_planet(struct pshine_planet *planet, float radius, float3 center) {
	planet->as_body.type = PSHINE_CELESTIAL_BODY_PLANET;
	planet->as_body.radius = radius;
	planet->as_body.parent_ref = NULL;
	*(float3*)planet->as_body.position.values = center;
	planet->atmosphere.density_falloff = 4.0f;
	planet->atmosphere.height = radius * 0.5f;
	planet->atmosphere.scattering_strength = 20.0f;
	planet->atmosphere.wavelengths[0] = 700;
	planet->atmosphere.wavelengths[1] = 530;
	planet->atmosphere.wavelengths[2] = 440;
}

static void deinit_planet(struct pshine_planet *planet) {
	(void)planet;
}

struct pshine_game_data {
	float camera_yaw, camera_pitch;
	float camera_dist;
	int movement_mode;
	uint8_t last_key_states[PSHINE_KEY_COUNT_];
};

void pshine_init_game(struct pshine_game *game) {
	game->data_own = calloc(1, sizeof(struct pshine_game_data));
	game->celestial_body_count = 2;
	game->celestial_bodies_own = calloc(game->celestial_body_count, sizeof(struct pshine_celestial_body*));
	game->celestial_bodies_own[0] = calloc(1, sizeof(struct pshine_planet));
	init_planet((void*)game->celestial_bodies_own[0], 1.0f, float3v0());
	game->celestial_bodies_own[1] = calloc(2, sizeof(struct pshine_planet));
	init_planet((void*)game->celestial_bodies_own[1], 5.0f, float3xyz(0.0f, -30.0f, 0.0f));
	game->camera_position.xyz.z = -40.0f;
	game->data_own->camera_dist = 20.0f;
	game->data_own->camera_yaw = 0.0f;
	game->data_own->camera_pitch = 0.0f;
	memset(game->data_own->last_key_states, 0, sizeof(game->data_own->last_key_states));
	game->atmo_blend_factor = 0.0f;
	game->data_own->movement_mode = 1;
}

void pshine_deinit_game(struct pshine_game *game) {
	for (size_t i = 0; i < game->celestial_body_count; ++i) {
		struct pshine_celestial_body *b = game->celestial_bodies_own[i];
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
	free(game->celestial_bodies_own);
	free(game->data_own);
}

static const enum pshine_key
	K_FORWARD = PSHINE_KEY_W,
	K_BACKWARD = PSHINE_KEY_S,
	K_UP = PSHINE_KEY_SPACE,
	K_DOWN = PSHINE_KEY_LEFT_SHIFT,
	K_RIGHT = PSHINE_KEY_D,
	K_LEFT = PSHINE_KEY_A,
	K_ZOOM_IN = PSHINE_KEY_Z,
	K_ZOOM_OUT = PSHINE_KEY_X;

static const float MOVE_SPEED = 1.0f;
static const float ROTATE_SPEED = 1.0f;

[[maybe_unused]]
static void update_camera_fly(struct pshine_game *game, float delta_time) {
	float3 delta = {};
	if (pshine_is_key_down(game->renderer, K_RIGHT)) delta.x += 1.0f;
	else if (pshine_is_key_down(game->renderer, K_LEFT)) delta.x -= 1.0f;
	if (pshine_is_key_down(game->renderer, K_UP)) delta.y += 1.0f;
	else if (pshine_is_key_down(game->renderer, K_DOWN)) delta.y -= 1.0f;
	if (pshine_is_key_down(game->renderer, K_FORWARD)) delta.z += 1.0f;
	else if (pshine_is_key_down(game->renderer, K_BACKWARD)) delta.z -= 1.0f;

	float3 cam_pos = float3vs(game->camera_position.values);
	cam_pos = float3add(cam_pos, float3mul(float3norm(delta), MOVE_SPEED * delta_time));

	// float3 cam_forward = float3norm(float3sub(float3vs(game->celestial_bodies_own[0]->position.values), cam_pos));
	float3 cam_forward = float3xyz(0.0f, 0.0f, 1.0f);

	*(float3*)game->camera_position.values = cam_pos;
	*(float3*)game->camera_forward.values = cam_forward;
}

[[maybe_unused]]
static void update_camera_arc(struct pshine_game *game, float delta_time) {
	float3 delta = {};
	if (pshine_is_key_down(game->renderer, K_RIGHT)) delta.x += 1.0f;
	else if (pshine_is_key_down(game->renderer, K_LEFT)) delta.x -= 1.0f;
	if (pshine_is_key_down(game->renderer, K_FORWARD)) delta.y += 1.0f;
	else if (pshine_is_key_down(game->renderer, K_BACKWARD)) delta.y -= 1.0f;

	if (pshine_is_key_down(game->renderer, K_ZOOM_IN))
		game->data_own->camera_dist += MOVE_SPEED * delta_time;
	else if (pshine_is_key_down(game->renderer, K_ZOOM_OUT))
		game->data_own->camera_dist -= MOVE_SPEED * delta_time;

	game->data_own->camera_pitch += delta.y * ROTATE_SPEED * delta_time;
	game->data_own->camera_yaw += delta.x * ROTATE_SPEED * delta_time;

	game->data_own->camera_pitch = clampf(game->data_own->camera_pitch, -π/2 + 0.1f, π/2 - 0.1f);

	float3 cam_pos = float3mul(float3xyz(
		cosf(game->data_own->camera_pitch) * sinf(game->data_own->camera_yaw),
		sinf(game->data_own->camera_pitch),
		-cosf(game->data_own->camera_pitch) * cosf(game->data_own->camera_yaw)
	), game->data_own->camera_dist);

	float3 cam_forward = float3norm(float3sub(float3vs(game->celestial_bodies_own[0]->position.values), cam_pos));

	*(float3*)game->camera_position.values = cam_pos;
	*(float3*)game->camera_forward.values = cam_forward;
}

void pshine_update_game(struct pshine_game *game, float delta_time) {
	if (pshine_is_key_down(game->renderer, PSHINE_KEY_C)) game->atmo_blend_factor += 0.5 * delta_time;
	else if (pshine_is_key_down(game->renderer, PSHINE_KEY_V)) game->atmo_blend_factor -= 0.5 * delta_time;
	game->atmo_blend_factor = clampf(game->atmo_blend_factor, 0.0f, 1.0f);

	if (pshine_is_key_down(game->renderer, PSHINE_KEY_F) && !game->data_own->last_key_states[PSHINE_KEY_F]) {
		game->data_own->movement_mode = !game->data_own->movement_mode;
	}

	{
		struct pshine_planet *p = (void*)game->celestial_bodies_own[0];

		if (pshine_is_key_down(game->renderer, PSHINE_KEY_UP))
			p->atmosphere.height += 0.2 * delta_time;
		else if (pshine_is_key_down(game->renderer, PSHINE_KEY_DOWN))
			p->atmosphere.height -= 0.2 * delta_time;
	}

	if (game->data_own->movement_mode) {
		update_camera_arc(game, delta_time);
	} else {
		update_camera_fly(game, delta_time);
	}
	
	memcpy(game->data_own->last_key_states, pshine_get_key_states(game->renderer), sizeof(uint8_t) * PSHINE_KEY_COUNT_);


	// PSHINE_DEBUG("cam pitch: %.2f yaw: %.2f", game->data_own->camera_pitch, game->data_own->camera_yaw);
}
