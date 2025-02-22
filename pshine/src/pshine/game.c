#include <pshine/game.h>
#include <string.h>
#include "math.h"
#include <pshine/util.h>
#include <cimgui/cimgui.h>

// Note: the math in this file is best viewed
//       with the Julia Mono font, or with UnifontEx.
//       Other fonts just don't support the unicode used,
//       or dont have it as a monospaced character, so
//       the alignment becomes wonky :(

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

#define TIME_FORMAT "%.0fy %.0fmo %.0fd %.0fh:%.0fm:%.3fs"
struct time_format_params {
	double years;
	double months;
	double days;
	double hours;
	double minutes;
	double seconds;
};
#define TIME_FORMAT_ARGS(P) ((P).years), ((P).months), ((P).days), ((P).hours), ((P).minutes), ((P).seconds)
static struct time_format_params compute_time_format_params(double secs) {
	struct time_format_params r = {0};
	r.seconds = secs;
	r.minutes = trunc(fabs(r.seconds)) / 60.0; r.seconds -= r.minutes * 60.0;
	r.hours = trunc(r.minutes / 60.0); r.minutes = fmod(r.minutes, 60.0);
	r.days = trunc(r.hours / 24.0); r.hours = fmod(r.hours, 24.0);
	r.months = trunc(r.days * 12.0 / 365.0); r.days = fmod(r.days, 365.0 / 12.0);
	r.years = trunc(r.months / 12.0); r.months = fmod(r.months, 12.0);
	return r;
}

typedef struct pshine_planet_vertex planet_vertex;

// requires: |a| = |b|
static inline float3 spheregen_float3lerp(float3 a, float3 b, float t) {
	float m2 = float3mag2(a);
	if (fabsf(m2) < 0.00001f) return float3lerp(a, b, t);
	float ϕ = acosf(float3dot(a, b) / m2);
	float isinϕ = 1.0f / sinf(ϕ);
	float
		ca = sinf((1 - t) * ϕ) * isinϕ,
		cb = sinf(t * ϕ) * isinϕ;
	return float3add(float3mul(a, ca), float3mul(b, cb));
}

// The next couple of functions are from https://www.jeremyong.com/graphics/2023/01/09/tangent-spaces-and-diamond-encoding/

// static inline float encode_diamond(float2 p) {
// 	// Project to the unit diamond, then to the x-axis.
// 	float x = p.x / (fabs(p.x) + fabs(p.y));

// 	// Contract the x coordinate by a factor of 4 to represent all 4 quadrants in
// 	// the unit range and remap
// 	float py_sign = copysign(1, p.y);
// 	return -py_sign * 0.25f * x + 0.5f + py_sign * 0.25f;
// }

// static inline float2 decode_diamond(float p) {
// 	float2 v;

// 	// Remap p to the appropriate segment on the diamond
// 	float p_sign = copysign(1, p - 0.5f);
// 	v.x = -p_sign * 4.f * p + 1.f + p_sign * 2.f;
// 	v.y = p_sign * (1.f - fabs(v.x));

// 	// Normalization extends the point on the diamond back to the unit circle
// 	return float2norm(v);
// }

// // Given a normal and tangent vector, encode the tangent as a single float that can be
// // subsequently quantized.
// static inline float encode_tangent(float3 normal, float3 tangent)
// {
// 	// First, find a canonical direction in the tangent plane
// 	float3 t1;
// 	if (fabs(normal.y) > fabs(normal.z))
// 	{
// 		// Pick a canonical direction orthogonal to n with z = 0
// 		t1 = float3xyz(normal.y, -normal.x, 0.f);
// 	}
// 	else
// 	{
// 		// Pick a canonical direction orthogonal to n with y = 0
// 		t1 = float3xyz(normal.z, 0.f, -normal.x);
// 	}
// 	t1 = float3norm(t1);

// 	// Construct t2 such that t1 and t2 span the plane
// 	float3 t2 = float3cross(t1, normal);

// 	// Decompose the tangent into two coordinates in the canonical basis
// 	float2 packed_tangent = float2xy(float3dot(tangent, t1), float3dot(tangent, t2));

// 	// Apply our diamond encoding to our two coordinates
// 	return encode_diamond(packed_tangent);
// }

// static inline float3 decode_tangent(float3 normal, float diamond_tangent) {
// 	// As in the encode step, find our canonical tangent basis span(t1, t2)
// 	float3 t1;
// 	if (fabs(normal.y) > fabs(normal.z))
// 	{
// 		t1 = float3xyz(normal.y, -normal.x, 0.f);
// 	}
// 	else
// 	{
// 		t1 = float3xyz(normal.z, 0.f, -normal.x);
// 	}
// 	t1 = float3norm(t1);

// 	float3 t2 = float3cross(t1, normal);

// 	// Recover the coordinates used with t1 and t2
// 	float2 packed_tangent = decode_diamond(diamond_tangent);

// 	return float3add(float3mul(t1, packed_tangent.x), float3mul(t2, packed_tangent.y));
// }

// From the unit vector survey paper
static inline float sign_not_zero(float v) {
	return (v >= 0.0) ? +1.0 : -1.0;
}
// Assume normalized input. Output is on [-1, 1] for each component.
static inline float2 float32x3_to_oct(float3 v) {
	// Project the sphere onto the octahedron, and then onto the xy plane
	float2 p = float2mul(float2vs(v.vs), (1.0 / (fabs(v.x) + fabs(v.y) + fabs(v.z))));
	// Reflect the folds of the lower hemisphere over the diagonals
	return (v.z <= 0.0) ? float2xy((1.0 - fabs(p.y)) * sign_not_zero(p.x), (1.0 - fabs(p.x)) * sign_not_zero(p.y)) : p;
}

static inline float3 oct_to_float32x3(float2 e) {
	float3 v = float3xyz(e.x, e.y, 1.0 - fabs(e.x) - fabs(e.y));
	if (v.z < 0) v = float3xyz((1.0 - fabs(v.y)) * sign_not_zero(v.x), (1.0 - fabs(v.x)) * sign_not_zero(v.y), v.z);
	return float3norm(v);
}

// static inline pshine_snorm32 snorm32_float(float v) { return (pshine_snorm32){ (int32_t)roundf(v * INT32_MAX) }; }
// static inline pshine_snorm32x2 snorm32x2_float2(float2 v) { return (pshine_snorm32x2){ snorm32_float(v.x), snorm32_float(v.y) }; }
// static inline float float_snorm32(pshine_snorm32 v) { return (float)v.x / INT32_MAX; }
// static inline float2 float2_snorm32x2(pshine_snorm32x2 v) { return float2xy(float_snorm32(v.x), float_snorm32(v.y)); }
// static inline pshine_unorm32 unorm32_float(float v) { return (pshine_unorm32){ (uint32_t)roundf(v * UINT32_MAX) }; }
// static inline pshine_unorm32x2 unorm32x2_float2(float2 v) { return (pshine_unorm32x2){ unorm32_float(v.x), unorm32_float(v.y) }; }
// static inline float float_unorm32(pshine_unorm32 v) { return (float)v.x / UINT32_MAX; }
// static inline float2 float2_unorm32x2(pshine_unorm32x2 v) { return float2xy(float_unorm32(v.x), float_unorm32(v.y)); }

static inline planet_vertex spheregen_vtxlerp(
	const planet_vertex *a,
	const planet_vertex *b,
	float t
) {
	float3 pos = spheregen_float3lerp(float3vs(a->position), float3vs(b->position), t);
	float3 nor = spheregen_float3lerp(oct_to_float32x3(float2vs(a->normal_oct)), oct_to_float32x3(float2vs(b->normal_oct)), t);
	// float3 tng = spheregen_float3lerp(decode_tangent(nor, a->tangent_dia), decode_tangent(nor, b->tangent_dia), t);
	float2 nor_oct = float32x3_to_oct(nor);
	// float tng_dia = encode_tangent(nor, tng);
	return (planet_vertex){
		{ pos.x, pos.y, pos.z },
		{ nor_oct.x, nor_oct.y },
		0.0f,
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
			{ { +1,  0,  0 } },
			{ {  0,  0, +1 } },
			{ { -1,  0,  0 } },
			{ {  0,  0, -1 } },
			{ {  0, +1,  0 } },
			{ {  0, -1,  0 } },
		};
		// float3 tngs[6] = {
		// 	float3xyz( 0,  0, +1),
		// 	float3xyz(-1,  0,  0),
		// 	float3xyz( 0,  0, -1),
		// 	float3xyz(+1,  0,  0),
		// 	float3xyz( 0,  0,  0),
		// };
		for (int i = 0; i < 6; ++i) {
			float2 nor_oct = float32x3_to_oct(float3vs(vtxs[i].position));
			vtxs[i].normal_oct[0] = nor_oct.x;
			vtxs[i].normal_oct[1] = nor_oct.y;
			// vtxs[i].tangent_dia = encode_tangent(float3vs(vtxs[i].position), float3 tangent);
		}

/*


 o    o/  \o   \o/    o  .
/O\  /O    O\   O   -'O`-|
/ \  / \  / \  / \   / \ |

*/

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

[[maybe_unused]]
static void init_star(struct pshine_star *star) {
	star->as_body.type = PSHINE_CELESTIAL_BODY_STAR;
	star->as_body.is_static = true;
	star->as_body.radius = 695'700'000.0;
	star->as_body.parent_ref = NULL;
	star->as_body.gravitational_parameter = 132.71244;
	star->as_body.name = "Sol";
	star->as_body.gizmo_color = 0x97e6fc;
}

static void init_planet(
	struct pshine_planet *planet,
	struct pshine_celestial_body *parent,
	double radius,
	double3 center
) {
	planet->as_body.type = PSHINE_CELESTIAL_BODY_PLANET;
	planet->as_body.radius = radius;
	planet->as_body.mass = 1.0;
	planet->as_body.parent_ref = parent;
	planet->as_body.rotation_speed = -0.026178;
	planet->as_body.name = "Earth";
	planet->as_body.gizmo_color = 0xf9bf2c;
#define DEG2RAD π/180.0
	*(double3*)planet->as_body.rotation_axis.values
		= double3xyz(0.0, 1.0, 0.0)
		;
		// = double3norm(double3xyz(0.0, sin((90 - 23.44) * DEG2RAD), cos((90 - 23.44) * DEG2RAD)));
	planet->as_body.rotation = 0.0;
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
	planet->as_body.surface.albedo_texture_path = "data/textures/earth_5k.jpg";
	planet->as_body.surface.lights_texture_path = "data/textures/earth_lights_2k.jpg";
	planet->as_body.surface.bump_texture_path = "data/textures/earth_bump_2k.jpg";
	planet->as_body.surface.spec_texture_path = "data/textures/earth_spec_2k.jpg";

	struct pshine_orbit_info *orbit = &planet->as_body.orbit;
	orbit->argument = 0.0;
	orbit->eccentricity = 0.0167086;
	orbit->inclination = 0.0;
	orbit->longitude = 0.0;
	orbit->semimajor = 149598.023;
	orbit->true_anomaly = 0.0;
}

static void deinit_star(struct pshine_star *star) {
	(void)star;
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
	game->time_scale = 1.0;
	game->data_own = calloc(1, sizeof(struct pshine_game_data));
	game->celestial_body_count = 2;
	game->celestial_bodies_own = calloc(game->celestial_body_count, sizeof(struct pshine_celestial_body*));
	game->celestial_bodies_own[1] = calloc(1, sizeof(struct pshine_star));
	init_star((void*)game->celestial_bodies_own[1]);
	game->celestial_bodies_own[0] = calloc(1, sizeof(struct pshine_planet));
	init_planet((void*)game->celestial_bodies_own[0], game->celestial_bodies_own[1], 6'371'000.0, double3v0());
	// game->celestial_bodies_own[1] = calloc(2, sizeof(struct pshine_planet));
	// init_planet((void*)game->celestial_bodies_own[1], 5.0, double3xyz(0.0, -1'000'000.0, 0.0));
	game->data_own->camera_dist = game->celestial_bodies_own[0]->radius + 10'000'000.0;
	game->camera_position.xyz.z = -game->data_own->camera_dist;
	game->data_own->camera_yaw = 0.0;
	game->data_own->camera_pitch = 0.0;
	memset(game->data_own->last_key_states, 0, sizeof(game->data_own->last_key_states));
	game->atmo_blend_factor = 0.0;
	game->data_own->movement_mode = 1;
	game->data_own->move_speed = 500'000.0;
	game->material_smoothness_ = 0.02;
	*(double3*)game->sun_position.values = double3xyz(0, 0, 0);
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
			case PSHINE_CELESTIAL_BODY_STAR: deinit_star((void*)b); break;
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

	cam_pos = double3add(cam_pos, double3vs(game->celestial_bodies_own[0]->position.values));

	double3 cam_forward = double3norm(double3sub(double3vs(game->celestial_bodies_own[0]->position.values), cam_pos));
	// double3 cam_forward = double3xyz(-1.0f, 0.0f, 0.0f);

	*(double3*)game->camera_position.values = cam_pos;
	*(double3*)game->camera_forward.values = cam_forward;
}

static void propagate_orbit(struct pshine_game *game, float delta_time, struct pshine_orbit_info *orbit) {
	// https://orbital-mechanics.space/time-since-periapsis-and-keplers-equation/time-since-periapsis.html
	// https://orbital-mechanics.space/time-since-periapsis-and-keplers-equation/universal-variables.html
	// Also stuff stolen from https://git.sr.ht/~thepuzzlemaker/KerbalToolkit/tree/the-big-port/item/lib/src/kepler/orbits.rs
	// Thanks Wren :o)

	// We need to change the orbit's true anomaly (ν) based on the other parameters and the elapsed time.
	// The equations for the anomaly are different for different types of orbit, so we use a so-called
	// Universal anomaly here.
	//
	// The relation of the universal anomaly χ to the other anomalies:
	//
	//            ⎧ 
	//            ⎪ [TODO(tanν / 2)]        for parabolas, e = 1
	//            ⎪  ___
	//        χ = ⎨ √ a   E                 for ellipses, e < 1
	//            ⎪  ____
	//            ⎪ √ -a  F                 for hyperbolas, e > 1
	//            ⎩ 
	//
	// Let's define the Stumpff functions, useful in the Kepler equation:
	//                         _
	//              ⎧  1 - cos√z
	//              ⎪ ──────────╴,    if z > 0
	//              ⎪     z
	//              ⎪       __
	//              ⎪  cosh√-z - 1
	//       C(z) = ⎨ ────────────╴,  if z < 0
	//              ⎪      -z
	//              ⎪
	//              ⎪ 1
	//              ⎪ ─,              if z = 0.
	//              ⎩ 2
	//                  _       _
	//              ⎧  √z - sin√z
	//              ⎪ ────────────╴,    if z > 0
	//              ⎪     (√z)³
	//              ⎪       __    __
	//              ⎪  sinh√-z - √-z
	//       S(z) = ⎨ ──────────────╴,  if z < 0
	//              ⎪      (√-z)³
	//              ⎪
	//              ⎪ 1
	//              ⎪ ─,                if z = 0.
	//              ⎩ 6
	//
	// With some complicated maths, we can write the Kepler equation in terms of
	// the universal anomaly (χ):
	//
	//         r₀vᵣ₀
	//         ────╴ χ² C(αχ²) + (1 - αr₀)χ³ S(αχ²) + r₀χ = (t - t₀)√μ
	//          √μ
	//
	// Where   α = a⁻¹, t₀ is the initial time, r₀ is the initial position,
	//         vᵣ₀ is the initial projection of the velocity on the position vector,
	//         C(x) and S(x) are the Stumpff functions, defined above.
	//
	// We don't have the values for r₀ and v₀, but we know that at the apses
	// v ⟂ r => vᵣ₀ = 0. At the periapsis, r₀ is the distance from the vertex
	// to the focus, or semimajor axis minus the focal distance:
	//
	//        r₀ = a - ea = a(1 - e)
	//
	// Then, using t₀ = 0 at the periapsis, the equation becomes:
	//
	//        e χ³ S(χ²/a) + a(1 - e)χ - t√μ = 0 = f(χ)
	//
	// Unfrogtunately, this equation cannot be solved algebraically,
	// (since it is the Kepler equation [M = E - esinE], but reworded a bit),
	// so we need to use for example Newton's Method to find the roots.
	// Turns out, Laguerre algorithm is a bit better for this problem,
	// so we'll use it instead.
	//
	// For these algorithms, we need the derivative of our function:
	//
	//         df(χ)
	//        ──────╴ = e χ² C(χ²/a) + a(1 - e)
	//          dχ
	//
	// Once we find a good enough χ, we can figure out the anomalies that
	// we need, and change our orbit.

	double Δt = delta_time; // Change in time.
	double μ = 132.712344; // The gravitational parameter.
	double a = orbit->semimajor; // The semimajor axis.
	double e = orbit->eccentricity; // The eccentricity.

	double p = a * (1 - e*e);

	double u = NAN; // Mean motion.
	if (fabs(e - 1.0) < 1e-6) { // parabolic
		u = 2.0 * sqrt(μ / (p*p*p));
	} else if (e < 1.0) { // elliptic
		u = sqrt(μ / (a*a*a));
	} else if (e < 1.0) { // hyperbolic
		u = sqrt(μ / -(a*a*a));
	} else {
		unreachable();
	}

	double T = 2 * π / u; // Orbital period.
	(void)T;

	// Solve for χ using Newton's Method:
	game->time += Δt; // TODO: figure out t from the orbital params.
	// game->time = fmod(game->time, T);
	double tsqrtμ = game->time * sqrt(μ);
	double χ = tsqrtμ/fabs(a);
	double sqrtp = sqrt(a*(1.0 - e*e));
	{
		for (int i = 0; i < 50; ++i) {
			double αχ2 = χ*χ/a;
			double sqrtαχ2 = sqrt(fabs(αχ2));
			double Cαχ2 = NAN;
			{ // Stumpff's C(αχ²)
				if (fabs(αχ2) < 1e-6) Cαχ2 = 0.5;
				else if (αχ2 > 0.0) Cαχ2 = (1 - cos(sqrtαχ2)) / αχ2;
				else Cαχ2 = (cosh(sqrtαχ2) - 1) / -αχ2;
			}
			double Sαχ2 = NAN;
			{ // Stumpff's S(αχ²)
				if (fabs(αχ2) < 1e-6) Sαχ2 = 1./6.;
				else if (αχ2 > 0.0) Sαχ2 = (sqrtαχ2 - sin(sqrtαχ2)) / pow(sqrtαχ2, 3.0);
				else Sαχ2 = (sinh(sqrtαχ2) - sqrtαχ2) / pow(sqrtαχ2, 3.0);
			}
	 		double f = e * χ*χ*χ * Sαχ2 + a*(1-e) * χ - tsqrtμ;
			if (fabs(f) < 1e-3) break;
	 		double dfdχ = e * χ*χ * Cαχ2 + a*(1-e);
	 		χ -= f/dfdχ;
 		}
	}

	// TODO: Solving for χ using the Laguerre algorithm, which is supposedly better
	// {
	// 	double n = 5;
	// 	double χᵢ = 0.0;
	// 	double αχᵢ² = χᵢ*χᵢ / a;
	// 	double t = .0, t₀ = .0;
	// }

	// PSHINE_DEBUG("chi = %f", χ);

	if (fabs(e - 1) < 1e-6) { // parabolic
		orbit->true_anomaly = 2 * atan(χ/sqrtp);
	} else if (e < 1) {
		double E = χ/sqrt(a);
		orbit->true_anomaly = 2 * atan(tan(E/2)*sqrt((1+e)/(1-e)));
	} else if (e > 1) {
		double F = χ/sqrt(-a);
		orbit->true_anomaly = 2 * atan(tanh(F/2)*sqrt((e+1)/(e-1)));
	}
}

// returns only the position for now.
static double3 kepler_orbit_to_state_vector(const struct pshine_orbit_info *orbit) {
	// Thank god https://orbital-mechanics.space exists!
	// The conversion formulas are taken from
	//   /classical-orbital-elements/orbital-elements-and-the-state-vector.html#orbital-elements-state-vector
	// But for some reason we get the semimajor axis equation from here instead, which includes the angular momentum (that we need):
	//   /time-since-periapsis-and-keplers-equation/universal-variables.html#orbit-independent-solution-the-universal-anomaly

	// TODO: rewrite this to make more sense.
	// Here's the semimajor axis equation:
	//
	//             h²     1
	//        a = --- ----------.
	//             μ    1 - e²
	//
	// We could extract just h², but we actually need the h²/μ term, so:
	//
	//             h²
	//        p = --- = a(1 - e²).
	//             μ
	//
	// First, we get the position in the perifocal frame of reference (relative to the orbit basically):
	//
	//             ⎛ cos ν ⎞       p         ⎛ cos ν ⎞   a(1 - e²)
	//        rₚ = ⎜   0   ⎟ ------------- = ⎜   0   ⎟ -------------.
	//             ⎝ sin ν ⎠  1 + e cos ν    ⎝ sin ν ⎠  1 + e cos ν
	//
	// Then we transform the perifocal frame to the "global" frame, rotating along each axis with these matrices:
	//
	//             ⎛ cos -ω  -sin -ω  0 ⎞
	//        R₁ = ⎜ sin -ω   cos -ω  0 ⎟,
	//             ⎝   0        0     1 ⎠
	//
	//             ⎛ 1    0        0    ⎞
	//        R₂ = ⎜ 0  cos -i  -sin -i ⎟,
	//             ⎝ 0  sin -i   cos -i ⎠
	//
	//             ⎛ cos -Ω  -sin -Ω  0 ⎞
	//        R₃ = ⎜ sin -Ω   cos -Ω  0 ⎟;
	//             ⎝   0        0     1 ⎠
	//
	// Now we can finally get the global position:
	//
	//        r = rₚR, where R = R₁R₂R₃.
	//

	// Some variables to correspond with the math notation:
	double ν = orbit->true_anomaly;
	double e = orbit->eccentricity;
	double a = orbit->semimajor;
	double Ω = orbit->longitude;
	double i = orbit->inclination;
	double ω = orbit->argument;

	double3 rₚ = double3mul(double3xyz(cos(ν), 0.0, sin(ν)),
		1'000'000 * a * (1 - e*e) / (1 + e * cos(ν)));

	double3x3 R1;
	R1.v3s[0] = double3xyz(cos(-ω), -sin(-ω), 0.0);
	R1.v3s[1] = double3xyz(sin(-ω),  cos(-ω), 0.0);
	R1.v3s[2] = double3xyz(    0.0,      0.0, 1.0);
	double3x3 R2;
	R2.v3s[0] = double3xyz(1,       0,        0);
	R2.v3s[1] = double3xyz(0, cos(-i), -sin(-i));
	R2.v3s[2] = double3xyz(0, sin(-i),  cos(-i));
	double3x3 R3;
	R3.v3s[0] = double3xyz(cos(-Ω), -sin(-Ω), 0.0);
	R3.v3s[1] = double3xyz(sin(-Ω),  cos(-Ω), 0.0);
	R3.v3s[2] = double3xyz(    0.0,      0.0, 1.0);

	double3x3 R = R1;
	double3x3mul(&R, &R2);
	double3x3mul(&R, &R3);

	double3 r = double3x3mulv(&R, rₚ);

	return r;
}

static void update_celestial_body(struct pshine_game *game, float delta_time, struct pshine_celestial_body *body) {
	if (!body->is_static) {
		propagate_orbit(game, delta_time, &body->orbit);
		body->rotation += body->rotation_speed * delta_time;
		double3 position = kepler_orbit_to_state_vector(&body->orbit);
		*(double3*)&body->position = position;
	}
}

static bool eximgui_input_double3(const char *label, double *vs, double step, const char *format) {
	double3 step3 = double3v(step);
	double3 faststep3 = double3v(step * 100.0);
	return ImGui_InputScalarNEx(label, ImGuiDataType_Double, vs, 3, step3.vs, faststep3.vs, format, ImGuiInputTextFlags_None);
}

void pshine_update_game(struct pshine_game *game, float delta_time) {
	for (size_t i = 0; i < game->celestial_body_count; ++i) {
		update_celestial_body(game, delta_time * game->time_scale, game->celestial_bodies_own[i]);
	}

	if (pshine_is_key_down(game->renderer, PSHINE_KEY_F) && !game->data_own->last_key_states[PSHINE_KEY_F]) {
		game->data_own->movement_mode = !game->data_own->movement_mode;
	}

	if (pshine_is_key_down(game->renderer, PSHINE_KEY_F2) && !game->data_own->last_key_states[PSHINE_KEY_F2]) {
		if (pshine_is_key_down(game->renderer, PSHINE_KEY_LEFT_SHIFT)) {
			game->ui_dont_render_windows = !game->ui_dont_render_windows;
		} else {
			game->ui_dont_render_windows = game->ui_dont_render_gizmos = !game->ui_dont_render_gizmos;
		}
	}

	if (game->data_own->movement_mode) {
		update_camera_arc(game, delta_time);
	} else {
		update_camera_fly(game, delta_time);
	}

	memcpy(game->data_own->last_key_states, pshine_get_key_states(game->renderer), sizeof(uint8_t) * PSHINE_KEY_COUNT_);

#define SCSd3_WCSd3(wcs) (double3mul((wcs), PSHINE_SCS_FACTOR))
#define SCSd3_WCSp3(wcs) SCSd3_WCSd3(double3vs((wcs).values))
#define SCSd_WCSd(wcs) ((wcs) * PSHINE_SCS_FACTOR)

	if (!game->ui_dont_render_windows) {

		if (ImGui_Begin("Material", NULL, 0)) {
			ImGui_DragFloat("Bump scale", &game->material_smoothness_);
		}
		ImGui_End();

		if (ImGui_Begin("Orbit", NULL, 0)) {
			ImGui_Text("True anomaly: %.5f", game->celestial_bodies_own[0]->orbit.true_anomaly);
			double3 pos = (SCSd3_WCSp3(game->celestial_bodies_own[0]->position));
			ImGui_Text("Position (SCS): %.0f, %.0f, %.0f", pos.x, pos.y, pos.z);
			double μ = game->celestial_bodies_own[0]->parent_ref->gravitational_parameter;
			double a = game->celestial_bodies_own[0]->orbit.semimajor; // The semimajor axis.
			double e = game->celestial_bodies_own[0]->orbit.eccentricity; // The eccentricity.

			double p = a * (1 - e*e);

			double u = NAN; // Mean motion.
			if (fabs(e - 1.0) < 1e-6) { // parabolic
				u = 2.0 * sqrt(μ / (p*p*p));
			} else if (e < 1.0) { // elliptic
				u = sqrt(μ / (a*a*a));
			} else if (e < 1.0) { // hyperbolic
				u = sqrt(μ / -(a*a*a));
			} else {
				unreachable();
			}

			double T = 2 * π / u; // Orbital period.
			struct time_format_params time_fmt = compute_time_format_params(T);
			ImGui_Text("Period: " TIME_FORMAT, TIME_FORMAT_ARGS(time_fmt));
		}
		ImGui_End();

		if (ImGui_Begin("Camera", NULL, 0)) {
			double3 p = double3vs(game->camera_position.values);
			double3 p_scs = double3mul(p, PSHINE_SCS_FACTOR);
			double d = double3mag(double3sub(p, double3vs(game->celestial_bodies_own[0]->position.values))) - game->celestial_bodies_own[0]->radius;
			enum si_prefix d_prefix = find_optimal_si_prefix(d);
			double d_scaled = apply_si_prefix(d_prefix, d);
			eximgui_input_double3("WCS Pos.", game->camera_position.values, 1000.0, "%.3fm");
			if (eximgui_input_double3("SCS Pos.", p_scs.vs, 100.0, "%.3fu")) {
				*(double3*)&game->camera_position.values = double3mul(p_scs, PSHINE_SCS_SCALE);
			}
			// ImGui_Text("WCS Position: %.3fm %.3fm %.3fm", p.x, p.y, p.z);
			// ImGui_Text("SCS Position: %.3fu %.3fu %.3fu", p.x * PSHINE_SCS_FACTOR, p.y * PSHINE_SCS_FACTOR, p.z * PSHINE_SCS_FACTOR);
			ImGui_Text("Distance from planet surface: %.3f %s m", d_scaled, si_prefix_english(d_prefix));
			{
				float v = game->data_own->move_speed;
				ImGui_DragFloatEx("movement speed, m/s", &v, 100.0f, 0.0f, PSHINE_SPEED_OF_LIGHT * 0.01f, "%.3f", 0);
				game->data_own->move_speed = v;
			}
			ImGui_Text("Yaw: %.3frad, Pitch: %.3frad", game->data_own->camera_yaw, game->data_own->camera_pitch);
			if (ImGui_Button("Reset rotation")) {
				game->data_own->camera_yaw = 0.0;
				game->data_own->camera_pitch = 0.0;
			}
			ImGui_SameLine();
			if (ImGui_Button("Reset position")) {
				game->data_own->camera_yaw = 0.0;
				game->data_own->camera_pitch = 0.0;
			}
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
			// float3 p = float3_double3(double3vs(game->sun_direction_.values));
			// if (ImGui_SliderFloat3("Sun", p.vs, -1.0f, 1.0)) {
			// 	*(double3*)game->sun_direction_.values = double3_float3(p);
			// }
			ImGui_SliderFloat("Time scale", &game->time_scale, 0.0, 1000.0);
			struct time_format_params time_fmt = compute_time_format_params(game->time);
			ImGui_Text("Time: " TIME_FORMAT, TIME_FORMAT_ARGS(time_fmt));
		}
		ImGui_End();
	}
}
