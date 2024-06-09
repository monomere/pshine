#include <pshine/game.h>
#include <string.h>
#include "math.h"
#include <pshine/util.h>
#include <cimgui/cimgui.h>

enum si_prefix {
	SI_ONE,
	SI_KILO,
	SI_MEGA,
	SI_GIGA,
	SI_PETA,
	SI_EXA,
	SI_ZETTA,
	SI_YOTTA,
	SI_RONNA,
	SI_QUETTA,
};

static enum si_prefix find_optimal_si_prefix(double value) {
	if (value < 1'000.0) return SI_ONE;
	if (value < 1'000'000.0) return SI_KILO;
	if (value < 1'000'000'000.0) return SI_MEGA;
	if (value < 1'000'000'000'000.0) return SI_GIGA;
	if (value < 1'000'000'000'000'000.0) return SI_PETA;
	return SI_EXA;
}

[[maybe_unused]]
static const char *si_prefix_string(enum si_prefix p) {
	switch (p) {
	case SI_ONE:    return "";
	case SI_KILO:   return "k";
	case SI_MEGA:   return "M";
	case SI_GIGA:   return "G";
	case SI_PETA:   return "P";
	case SI_EXA:    return "E";
	case SI_ZETTA:  return "Z";
	case SI_YOTTA:  return "Y";
	case SI_RONNA:  return "R";
	case SI_QUETTA: return "Q";
	}
	return "";
}

static const char *si_prefix_english(enum si_prefix p) {
	switch (p) {
	case SI_ONE:    return "";
	case SI_KILO:   return "thousand";
	case SI_MEGA:   return "million";
	case SI_GIGA:   return "billion";
	case SI_PETA:   return "trillion";
	case SI_EXA:    return "quadrillion";
	case SI_ZETTA:  return "quntillion";
	case SI_YOTTA:  return "sextillion";
	case SI_RONNA:  return "septillion";
	case SI_QUETTA: return "octillion";
	}
	return "";
}

static double apply_si_prefix(enum si_prefix p, double value) {
	switch (p) {
	case SI_ONE:    return value;
	case SI_KILO:   return value / 1'000.0;
	case SI_MEGA:   return value / 1'000'000.0;
	case SI_GIGA:   return value / 1'000'000'000.0;
	case SI_PETA:   return value / 1'000'000'000'000.0;
	case SI_EXA:    return value / 1'000'000'000'000'000.0;
	case SI_ZETTA:  return value / 1'000'000'000'000'000'000.0;
	case SI_YOTTA:  return value / 1'000'000'000'000'000'000'000.0;
	case SI_RONNA:  return value / 1'000'000'000'000'000'000'000'000.0;
	case SI_QUETTA: return value / 1'000'000'000'000'000'000'000'000'000.0;
	}
	return value;
}

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
	generate_sphere_mesh(48, out_mesh);
}

static void init_planet(struct pshine_planet *planet, double radius, double3 center) {
	planet->as_body.type = PSHINE_CELESTIAL_BODY_PLANET;
	planet->as_body.radius = radius;
	planet->as_body.parent_ref = NULL;
	*(double3*)planet->as_body.position.values = center;
	planet->has_atmosphere = true;
	// similar to Earth, the radius is 6371km
	// and the atmosphere height is 100km
	planet->atmosphere.height = 0.015696123 * radius;
	planet->atmosphere.rayleigh_coefs[0] = 3.8f;
	planet->atmosphere.rayleigh_coefs[1] = 13.5f;
	planet->atmosphere.rayleigh_coefs[2] = 33.1f;
	planet->atmosphere.rayleigh_falloff = 13.5f;
	planet->atmosphere.mie_coef = 20.1f;
	planet->atmosphere.mie_ext_coef = 1.1f;
	planet->atmosphere.mie_g_coef = -0.87f;
	planet->atmosphere.mie_falloff = 18.0f;
	planet->atmosphere.intensity = 20.0f;
}

static void deinit_planet(struct pshine_planet *planet) {
	(void)planet;
}

struct ship {
	double3 pos;

};

struct pshine_game_data {
	double camera_yaw, camera_pitch;
	double camera_dist;
	int movement_mode;
	double move_speed;
	uint8_t last_key_states[PSHINE_KEY_COUNT_];
};

void pshine_init_game(struct pshine_game *game) {
	game->data_own = calloc(1, sizeof(struct pshine_game_data));
	game->celestial_body_count = 1;
	game->celestial_bodies_own = calloc(game->celestial_body_count, sizeof(struct pshine_celestial_body*));
	game->celestial_bodies_own[0] = calloc(1, sizeof(struct pshine_planet));
	init_planet((void*)game->celestial_bodies_own[0], 6'371'000.0, double3v0());
	// game->celestial_bodies_own[1] = calloc(2, sizeof(struct pshine_planet));
	// init_planet((void*)game->celestial_bodies_own[1], 5.0, double3xyz(0.0, -1'000'000.0, 0.0));
	game->data_own->camera_dist = game->celestial_bodies_own[0]->radius + 15'000.0;
	game->camera_position.xyz.z = -game->data_own->camera_dist;
	game->data_own->camera_yaw = 0.0;
	game->data_own->camera_pitch = 0.0;
	memset(game->data_own->last_key_states, 0, sizeof(game->data_own->last_key_states));
	game->atmo_blend_factor = 0.0;
	game->data_own->movement_mode = 1;
	game->data_own->move_speed = 500'000.0; // PSHINE_SPEED_OF_LIGHT;
	game->sun_direction_.xyz.x = -1.0;
	game->sun_direction_.xyz.y =  0.0;
	game->sun_direction_.xyz.z =  0.0;
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

static const double ROTATE_SPEED = 1.0;

[[maybe_unused]]
static void update_camera_fly(struct pshine_game *game, float delta_time) {
	{
		double3 delta = {};
		if (pshine_is_key_down(game->renderer, PSHINE_KEY_LEFT)) delta.x += 1.0;
		else if (pshine_is_key_down(game->renderer, PSHINE_KEY_RIGHT)) delta.x -= 1.0;
		if (pshine_is_key_down(game->renderer, PSHINE_KEY_UP)) delta.y += 1.0;
		else if (pshine_is_key_down(game->renderer, PSHINE_KEY_DOWN)) delta.y -= 1.0;
		game->data_own->camera_pitch += delta.y * ROTATE_SPEED * delta_time;
		game->data_own->camera_yaw += delta.x * ROTATE_SPEED * delta_time;
	}

	double3x3 mat = {};
	setdouble3x3rotation(&mat, 0, game->data_own->camera_pitch, game->data_own->camera_yaw);

	double3 cam_forward = double3x3mulv(&mat, double3xyz(0, 0, 1));

	double3 cam_pos = double3vs(game->camera_position.values);
	{
		double3 delta = {};
		if (pshine_is_key_down(game->renderer, K_RIGHT)) delta.x += 1.0;
		else if (pshine_is_key_down(game->renderer, K_LEFT)) delta.x -= 1.0;
		if (pshine_is_key_down(game->renderer, K_UP)) delta.y += 1.0;
		else if (pshine_is_key_down(game->renderer, K_DOWN)) delta.y -= 1.0;
		if (pshine_is_key_down(game->renderer, K_FORWARD)) delta.z += 1.0;
		else if (pshine_is_key_down(game->renderer, K_BACKWARD)) delta.z -= 1.0;
		delta = double3x3mulv(&mat, delta);
		cam_pos = double3add(cam_pos, double3mul(double3norm(delta), game->data_own->move_speed * delta_time));
	}


	*(double3*)game->camera_position.values = cam_pos;
	*(double3*)game->camera_forward.values = cam_forward;
}

[[maybe_unused]]
static void update_camera_arc(struct pshine_game *game, float delta_time) {
	double3 delta = {};
	if (pshine_is_key_down(game->renderer, K_RIGHT)) delta.x += 1.0;
	else if (pshine_is_key_down(game->renderer, K_LEFT)) delta.x -= 1.0;
	if (pshine_is_key_down(game->renderer, K_FORWARD)) delta.y += 1.0;
	else if (pshine_is_key_down(game->renderer, K_BACKWARD)) delta.y -= 1.0;

	if (pshine_is_key_down(game->renderer, K_ZOOM_IN))
		game->data_own->camera_dist += game->data_own->move_speed * delta_time;
	else if (pshine_is_key_down(game->renderer, K_ZOOM_OUT))
		game->data_own->camera_dist -= game->data_own->move_speed * delta_time;

	game->data_own->camera_pitch += delta.y * ROTATE_SPEED * delta_time;
	game->data_own->camera_yaw += delta.x * ROTATE_SPEED * delta_time;

	game->data_own->camera_pitch = clampd(game->data_own->camera_pitch, -π/2 + 0.1, π/2 - 0.1);

	double3 cam_pos = double3mul(double3xyz(
		cos(game->data_own->camera_pitch) * sin(game->data_own->camera_yaw),
		sin(game->data_own->camera_pitch),
		-cos(game->data_own->camera_pitch) * cos(game->data_own->camera_yaw)
	), game->data_own->camera_dist);

	double3 cam_forward = double3norm(double3sub(double3vs(game->celestial_bodies_own[0]->position.values), cam_pos));
	// double3 cam_forward = double3xyz(-1.0f, 0.0f, 0.0f);

	*(double3*)game->camera_position.values = cam_pos;
	*(double3*)game->camera_forward.values = cam_forward;
}

void pshine_update_game(struct pshine_game *game, float delta_time) {
	if (pshine_is_key_down(game->renderer, PSHINE_KEY_C)) game->atmo_blend_factor += 0.5 * delta_time;
	else if (pshine_is_key_down(game->renderer, PSHINE_KEY_V)) game->atmo_blend_factor -= 0.5 * delta_time;
	game->atmo_blend_factor = clampd(game->atmo_blend_factor, 0.0, 1.0);

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

	if (ImGui_Begin("Camera", NULL, 0)) {
		double3 p = double3vs(game->camera_position.values);
		double d = double3mag(double3sub(p, double3vs(game->celestial_bodies_own[0]->position.values))) - game->celestial_bodies_own[0]->radius;
		enum si_prefix d_prefix = find_optimal_si_prefix(d);
		double d_scaled = apply_si_prefix(d_prefix, d);
		ImGui_Text("WCS Position: %.3fm %.3fm %.3fm", p.x, p.y, p.z);
		ImGui_Text("SCS Position: %.3fu %.3fu %.3fu", p.x * PSHINE_SCS_FACTOR, p.y * PSHINE_SCS_FACTOR, p.z * PSHINE_SCS_FACTOR);
		ImGui_Text("Distance from planet surface: %.3f %s m", d_scaled, si_prefix_english(d_prefix));
		{
			float v = game->data_own->move_speed;
			ImGui_DragFloatEx("movement speed, m/s", &v, 100.0f, 0.0f, PSHINE_SPEED_OF_LIGHT * 0.01f, "%.3f", 0);
			game->data_own->move_speed = v;
		}
		ImGui_Text("yaw: %.3frad, pitch: %.3frad", game->data_own->camera_yaw, game->data_own->camera_pitch);
	}
	ImGui_End();

	if (ImGui_Begin("Atmosphere", NULL, 0)) {
		struct pshine_planet *planet =(struct pshine_planet*)game->celestial_bodies_own[0];
		struct pshine_atmosphere_info *atmo = &planet->atmosphere;
		ImGui_SliderFloat3("Rayleigh Coefs.", atmo->rayleigh_coefs, 0.001f, 50.0f);
		ImGui_SliderFloat("Rayleigh Falloff.", &atmo->rayleigh_falloff, 0.0001f, 100.0f);
		ImGui_SliderFloat("Mie Coef.", &atmo->mie_coef, 0.001f, 50.0f);
		ImGui_SliderFloat("Mie Ext. Coef.", &atmo->mie_ext_coef, 0.001f, 5.0f);
		ImGui_SliderFloat("Mie 'g' Coef.", &atmo->mie_g_coef, -0.9999f, 0.9999f);
		ImGui_SliderFloat("Mie Falloff.", &atmo->mie_falloff, 0.0001f, 100.0f);
		ImGui_SliderFloat("Intensity", &atmo->intensity, 0.0f, 50.0f);
	}
	ImGui_End();

	if (ImGui_Begin("System", NULL, 0)) {
		float3 p = float3_double3(double3vs(game->sun_direction_.values));
		if (ImGui_SliderFloat3("Sun", p.vs, -1.0f, 1.0)) {
			*(double3*)game->sun_direction_.values = double3_float3(p);
		}
	}
	ImGui_End();

	// PSHINE_DEBUG("cam pitch: %.2f yaw: %.2f", game->data_own->camera_pitch, game->data_own->camera_yaw);
}
