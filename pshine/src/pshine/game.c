#include <pshine/game.h>
#include <string.h>
#include "math.h"
#include <pshine/util.h>
#include <cimgui/cimgui.h>
#include <toml.h>
#include <errno.h>

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
	value = fabs(value);
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
	r.seconds = fabs(secs);
	r.minutes = trunc(r.seconds / 60.0); r.seconds = fmod(r.seconds, 60.0);
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

void pshine_generate_planet_mesh(
	const struct pshine_planet *planet,
	struct pshine_mesh_data *out_mesh,
	size_t lod
) {
	const size_t lods[5] = { 96, 32, 24, 16, 8 };
	generate_sphere_mesh(lod >= 5 ? 8 : lods[lod], out_mesh);
}

static void propagate_orbit(double time, double gravitational_parameter, struct pshine_orbit_info *body);
static double3 kepler_orbit_to_state_vector(const struct pshine_orbit_info *orbit);

static void create_orbit_points(
	struct pshine_celestial_body *body,
	size_t count
) {
	body->orbit.cached_point_count = count;
	body->orbit.cached_points_own = calloc(count, sizeof(pshine_point3d_scaled));

	// Copy the orbit info because the true anomaly is changed
	// by the propagator.
	struct pshine_orbit_info o2 = body->orbit;

	double e = body->orbit.eccentricity;
	double a = body->orbit.semimajor;
	double μ = body->parent_ref->gravitational_parameter;
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
	double ti = T / (double)body->orbit.cached_point_count;

	double time = 0.0;
	for (size_t i = 0; i < body->orbit.cached_point_count; ++i) {
		propagate_orbit(time, μ, &o2);
		double3 pos = double3mul(kepler_orbit_to_state_vector(&o2), PSHINE_SCS_FACTOR);
		*(double3*)&body->orbit.cached_points_own[i] = pos;
		time += ti;
	}
}

static struct pshine_celestial_body *load_celestial_body(
	const char *fpath
) {
	FILE *fin = fopen(fpath, "rb");
	if (fin == nullptr) {
		PSHINE_ERROR("Failed to open celestial body file %s: %s", fpath,
			strerror(errno));
		return nullptr;
	}
	char *errbuf = calloc(1024, 1);
	toml_table_t *tab = toml_parse_file(fin, errbuf, 1024);
	fclose(fin);
	if (tab == nullptr) {
		PSHINE_ERROR("Failed to parse celestial body configuration:\n%s", errbuf);
		free(errbuf);
		return nullptr;
	}
	struct pshine_celestial_body *body = nullptr;
	toml_table_t *ptab = nullptr;

	toml_datum_t dat;
#define READ_FIELD(tab, NAME, FIELD_NAME, TYPE, DAT_FIELD) \
	dat = toml_##TYPE##_in(tab, NAME); if (dat.ok) FIELD_NAME = dat.u.DAT_FIELD
#define READ_FIELD_AT(arr, IDX, FIELD_NAME, TYPE, DAT_FIELD) \
	dat = toml_##TYPE##_at(arr, IDX); if (dat.ok) FIELD_NAME = dat.u.DAT_FIELD

	// no need for pshine_strdup, as dat.u.s is allocated by tomlc99.
#define READ_STR_FIELD(tab, NAME, FIELD_NAME) \
	dat = toml_string_in(tab, NAME); if (dat.ok) FIELD_NAME = dat.u.s

	if ((ptab = toml_table_in(tab, "planet")) != nullptr) {
		body = calloc(1, sizeof(struct pshine_planet));
		body->type = PSHINE_CELESTIAL_BODY_PLANET;
		struct pshine_planet *planet = (void*)body;
		toml_table_t *atab = toml_table_in(tab, "atmosphere");
		planet->has_atmosphere = false;
		if (atab != nullptr) {
			planet->has_atmosphere = true;
			READ_FIELD(atab, "height", planet->atmosphere.height, double, d);
			toml_array_t *arr = toml_array_in(atab, "rayleigh_coefs");
			if (arr != nullptr) {
				if (toml_array_nelem(arr) != 3) {
					PSHINE_WARN("Invalid celestial body configuration: atmosphere.rayleigh_coefs "
						"must be a 3 element array.");
				} else {
					READ_FIELD_AT(arr, 0, planet->atmosphere.rayleigh_coefs[0], double, d);
					READ_FIELD_AT(arr, 1, planet->atmosphere.rayleigh_coefs[1], double, d);
					READ_FIELD_AT(arr, 2, planet->atmosphere.rayleigh_coefs[2], double, d);
				}
			}
			struct pshine_elemental_composition *composition = &planet->atmosphere.composition;
			composition->constituent_count = 0;
			arr = toml_array_in(atab, "composition");
			if (arr != nullptr) {
				size_t n = toml_array_nelem(arr);
				if (n % 2 != 0) {
					PSHINE_WARN("Invalid celestial body configuration: the length of atmosphere.composition "
						"must be a multiple of two.");
					n -= 1;
				}
				if (n > 0) {
					composition->constituent_count = n / 2;
					// TODO: free
					composition->constituents_own = calloc(composition->constituent_count,
						sizeof(*composition->constituents_own));
					for (size_t i = 0; i < n; i += 2) {
						READ_FIELD_AT(arr, i + 0, composition->constituents_own[i / 2].fraction, double, d);
						READ_FIELD_AT(arr, i + 1, composition->constituents_own[i / 2].name_own, string, s);
					}
				}
			}
			READ_FIELD(atab, "rayleigh_falloff", planet->atmosphere.rayleigh_falloff, double, d);
			READ_FIELD(atab, "mie_coef", planet->atmosphere.mie_coef, double, d);
			READ_FIELD(atab, "mie_ext_coef", planet->atmosphere.mie_ext_coef, double, d);
			READ_FIELD(atab, "mie_g_coef", planet->atmosphere.mie_g_coef, double, d);
			READ_FIELD(atab, "mie_falloff", planet->atmosphere.mie_falloff, double, d);
			READ_FIELD(atab, "intensity", planet->atmosphere.intensity, double, d);
		}
	} else if ((ptab = toml_table_in(tab, "star")) != nullptr) {
		body = calloc(1, sizeof(struct pshine_star));
		body->type = PSHINE_CELESTIAL_BODY_STAR;
		struct pshine_star *star = (void*)body;
		READ_FIELD(ptab, "surface_temperature", star->temperature, double, d);
	} else {
		PSHINE_ERROR("Invalid celestial body configuration: no [planet] or [star] tables.");
		free(errbuf);
		return nullptr;
	}
	PSHINE_CHECK(ptab != nullptr, "what the");

	READ_STR_FIELD(ptab, "name", body->name_own);
	READ_STR_FIELD(ptab, "description", body->desc_own);

	// Remove newlines
	if (body->desc_own != nullptr) {
		for (char *found = body->desc_own; (found = strchr(found, '\n')) != nullptr;) {
			*(found++) = ' ';
		}
	}

	READ_STR_FIELD(ptab, "parent", body->tmp_parent_ref_name_own);

	READ_FIELD(ptab, "radius", body->radius, double, d);
	READ_FIELD(ptab, "mass", body->mass, double, d);
	READ_FIELD(ptab, "mu", body->gravitational_parameter, double, d);
	READ_FIELD(ptab, "average_color", body->average_color, int, i);
	READ_FIELD(ptab, "gizmo_color", body->gizmo_color, int, i);
	READ_FIELD(ptab, "is_static", body->is_static, bool, b);

	toml_table_t *otab = toml_table_in(tab, "orbit");
	if (otab != nullptr) {
		READ_FIELD(otab, "argument", body->orbit.argument, double, d);
		READ_FIELD(otab, "eccentricity", body->orbit.eccentricity, double, d);
		READ_FIELD(otab, "inclination", body->orbit.inclination, double, d);
		READ_FIELD(otab, "longitude", body->orbit.longitude, double, d);
		READ_FIELD(otab, "semimajor", body->orbit.semimajor, double, d);
		READ_FIELD(otab, "true_anomaly", body->orbit.true_anomaly, double, d);
	}

	toml_table_t *stab = toml_table_in(tab, "surface");
	if (stab != nullptr) {
		READ_STR_FIELD(stab, "albedo_texture_path", body->surface.albedo_texture_path_own);
		READ_STR_FIELD(stab, "spec_texture_path", body->surface.spec_texture_path_own);
		READ_STR_FIELD(stab, "lights_texture_path", body->surface.lights_texture_path_own);
		READ_STR_FIELD(stab, "bump_texture_path", body->surface.bump_texture_path_own);
	}

	body->rings.has_rings = false;
	toml_table_t *gtab = toml_table_in(tab, "rings");
	if (gtab != nullptr) {
		body->rings.has_rings = true;
		READ_FIELD(gtab, "inner_radius", body->rings.inner_radius, double, d);
		READ_FIELD(gtab, "outer_radius", body->rings.outer_radius, double, d);
		READ_FIELD(gtab, "shadow_smoothing", body->rings.shadow_smoothing, double, d);
		READ_STR_FIELD(gtab, "slice_texture_path", body->rings.slice_texture_path_own);
		if (body->rings.slice_texture_path_own == nullptr) {
			body->rings.has_rings = false;
			PSHINE_ERROR("No ring texture path present, ignoring rings.");
		}
	}

	toml_table_t *rtab = toml_table_in(tab, "rotation");
	if (rtab != nullptr) {
		READ_FIELD(rtab, "speed", body->rotation_speed, double, d);
		toml_array_t *arr = toml_array_in(rtab, "axis");
		if (arr != nullptr) {
			if (toml_array_nelem(arr) != 3) {
				PSHINE_WARN("Invalid celestial body configuration: rotation.axis "
					"must be a 3 element array.");
			} else {
				READ_FIELD_AT(arr, 0, body->rotation_axis.xyz.x, double, d);
				READ_FIELD_AT(arr, 1, body->rotation_axis.xyz.y, double, d);
				READ_FIELD_AT(arr, 2, body->rotation_axis.xyz.z, double, d);
			}
		}
		READ_FIELD(rtab, "speed", body->rotation_speed, double, d);
		READ_STR_FIELD(rtab, "spec_texture_path", body->surface.spec_texture_path_own);
	}

#undef READ_FIELD
#undef READ_FIELD_AT
#undef READ_STR_FIELD

	toml_free(tab);

	free(errbuf);
	return body;
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

struct eximgui_state {
	ImGuiStorage storage;
	ImFont *font_regular;
	ImFont *font_bold;
	ImFont *font_italic;
	ImFont *font_title;
};

static void eximgui_state_init(struct eximgui_state *st);
static void eximgui_state_deinit(struct eximgui_state *st);

enum movement_mode {
	MOVEMENT_FLY,
	MOVEMENT_ARC,
	MOVEMENT_WALK,
};

struct pshine_game_data {
	double camera_yaw, camera_pitch;
	double camera_dist;
	enum movement_mode movement_mode;
	double move_speed;
	uint8_t last_key_states[PSHINE_KEY_COUNT_];
	size_t selected_body;
	struct eximgui_state eximgui_state;
	bool is_control_precise;
};

static void load_star_system_config(struct pshine_game *game, struct pshine_star_system *out, const char *fpath) {
	FILE *fin = fopen(fpath, "rb");
	if (fin == nullptr) {
		PSHINE_ERROR("Failed to open star system config file '%s': %s", fpath,
			strerror(errno));
		return;
	}
	char *errbuf = calloc(1024, 1);
	toml_table_t *tab = toml_parse_file(fin, errbuf, 1024);
	fclose(fin);
	if (tab == nullptr) {
		PSHINE_ERROR("Failed to parse star system config:\n%s", errbuf);
		free(errbuf);
		return;
	}
	toml_array_t *arr = toml_array_in(tab, "planets");
	out->body_count = 0;
	if (arr != nullptr) {
		out->body_count = toml_array_nelem(arr);
		out->bodies_own = calloc(out->body_count, sizeof(struct pshine_celestial_body *));
		for (size_t i = 0; i < out->body_count; ++i) {
			toml_datum_t body_fpath = toml_string_at(arr, i);
			if (!body_fpath.ok) {
				PSHINE_ERROR("star system config planets[%zu] isn't a string", i);
				continue;
			}
			out->bodies_own[i] = load_celestial_body(body_fpath.u.s);
			free(body_fpath.u.s);
		}
	}
	toml_datum_t name = toml_string_in(tab, "name");
	if (!name.ok) {
		PSHINE_ERROR("star system config name isn't a string");
		out->name_own = pshine_strdup("<config_error>");
	} else {
		out->name_own = name.u.s;
	}
}

static void load_game_config(struct pshine_game *game, const char *fpath) {
	FILE *fin = fopen(fpath, "rb");
	if (fin == nullptr) {
		PSHINE_ERROR("Failed to open game config file '%s': %s", fpath,
			strerror(errno));
		return;
	}
	char *errbuf = calloc(1024, 1);
	toml_table_t *tab = toml_parse_file(fin, errbuf, 1024);
	fclose(fin);
	if (tab == nullptr) {
		PSHINE_ERROR("Failed to parse game config:\n%s", errbuf);
		free(errbuf);
		return;
	}
	toml_array_t *arr = toml_array_in(tab, "systems");
	game->star_system_count = 0;
	if (arr != nullptr) {
		game->star_system_count = toml_array_nelem(arr);
		game->star_systems_own = calloc(game->star_system_count, sizeof(*game->star_systems_own));
		for (size_t i = 0; i < game->star_system_count; ++i) {
			toml_datum_t body_fpath = toml_string_at(arr, i);
			if (!body_fpath.ok) {
				PSHINE_ERROR("game config systems[%zu] isn't a string", i);
				continue;
			}
			load_star_system_config(game, &game->star_systems_own[i], body_fpath.u.s);
			free(body_fpath.u.s);
		}
	}
	game->environment.type = PSHINE_ENVIRONMENT_PROJECTION_EQUIRECTANGULAR;
	game->environment.texture_path_own = nullptr;
	toml_table_t *etab = toml_table_in(tab, "environment");
	if (etab != nullptr) {
		struct toml_datum_t path = toml_string_in(etab, "texture_path");
		if (path.ok) game->environment.texture_path_own = path.u.s;
		struct toml_datum_t proj = toml_string_in(etab, "projection");
		if (proj.ok) {
			if (strcmp(proj.u.s, "equirectangular") == 0)
				game->environment.type = PSHINE_ENVIRONMENT_PROJECTION_EQUIRECTANGULAR;
			else
				PSHINE_ERROR("game config environment.projection: unknown value '%s', "
					"keeping default equirectangular.", proj.u.s);
			free(proj.u.s);
		}
	}
	free(errbuf);
}

static void init_star_system(struct pshine_game *game, struct pshine_star_system *system) {
	for (size_t i = 0; i < system->body_count; ++i) {
		const char *name = system->bodies_own[i]->tmp_parent_ref_name_own;
		if (name == nullptr) continue;
		for (size_t j = 0; j < system->body_count; ++j) {
			if (strcmp(system->bodies_own[j]->name_own, name) == 0) {
				system->bodies_own[i]->parent_ref = system->bodies_own[j];
				PSHINE_DEBUG(
					"Setting parent_ref of %s to %s",
					system->bodies_own[i]->name_own,
					name
				);
				break;
			}
		}
		if (system->bodies_own[i]->parent_ref == nullptr) {
			PSHINE_ERROR(
				"No celestial body with name '%s' found (setting parent reference for %s)",
				name,
				system->bodies_own[i]->name_own
			);
		}
	}

	for (size_t i = 0; i < system->body_count; ++i) {
		if (!system->bodies_own[i]->is_static) {
			create_orbit_points(system->bodies_own[i], 1000);
		}
	}
}

void pshine_init_game(struct pshine_game *game) {
	{
		struct pshine_timeval now = pshine_timeval_now();
		pshine_pcg64_init(&game->rng64, now.sec, now.nsec);
	}
	game->time_scale = 1.0;
	game->data_own = calloc(1, sizeof(struct pshine_game_data));
	load_game_config(game, "data/config.toml");
	// game->celestial_bodies_own[4] = load_celestial_body("data/celestial/moon.toml");
	// game->celestial_bodies_own[3] = load_celestial_body("data/celestial/venus.toml");
	// game->celestial_bodies_own[2] = load_celestial_body("data/celestial/mars.toml");
	// game->celestial_bodies_own[1] = load_celestial_body("data/celestial/sun.toml");
	// game->celestial_bodies_own[0] = load_celestial_body("data/celestial/earth.toml");

	for (size_t i = 0; i < game->star_system_count; ++i) {
		init_star_system(game, &game->star_systems_own[i]);
	}

	// game->celestial_bodies_own[1] = calloc(1, sizeof(struct pshine_star));
	// init_star((void*)game->celestial_bodies_own[1]);
	// game->celestial_bodies_own[0] = calloc(1, sizeof(struct pshine_planet));
	// init_planet((void*)game->celestial_bodies_own[0], game->celestial_bodies_own[1], 6'371'000.0, double3v0());
	// game->celestial_bodies_own[1] = calloc(2, sizeof(struct pshine_planet));
	// init_planet((void*)game->celestial_bodies_own[1], 5.0, double3xyz(0.0, -1'000'000.0, 0.0));

	if (game->star_system_count <= 0) {
		PSHINE_PANIC("No star systems present, there's nothing to show; exiting.");
	}
	game->current_star_system = 0;
	game->data_own->selected_body = 0;
	game->data_own->camera_dist = game->star_systems_own[game->current_star_system].bodies_own[0]->radius + 165'000'000.0;
	game->camera_position.xyz.z = -game->data_own->camera_dist;
	game->data_own->camera_yaw = π/2;
	game->data_own->camera_pitch = 0.0;
	memset(game->data_own->last_key_states, 0, sizeof(game->data_own->last_key_states));
	game->atmo_blend_factor = 0.0;
	game->data_own->movement_mode = MOVEMENT_FLY;
	game->data_own->move_speed = 500'000.0;
	game->time_scale = 0.0;
	game->camera_position.xyz.x = 31483290.911 * PSHINE_SCS_SCALE;
	game->camera_position.xyz.y = 75.221 * PSHINE_SCS_SCALE;
	game->camera_position.xyz.z = 13965308.151 * PSHINE_SCS_SCALE;
	game->graphics_settings.camera_fov = 60.0;
	game->graphics_settings.exposure = 1.0;
	game->graphics_settings.bloom_knee = 7.0;
	game->graphics_settings.bloom_threshold = 7.0;
	game->material_smoothness_ = 0.02;
	game->data_own->is_control_precise = false;

	eximgui_state_init(&game->data_own->eximgui_state);

}

static void deinit_star_system(struct pshine_game *game, struct pshine_star_system *system) {
	for (size_t i = 0; i < system->body_count; ++i) {
		struct pshine_celestial_body *b = system->bodies_own[i];
		if (b == NULL) {
			PSHINE_WARN("null celestial body");
			continue;
		}
		switch (b->type) {
			case PSHINE_CELESTIAL_BODY_PLANET: deinit_planet((void*)b); break;
			case PSHINE_CELESTIAL_BODY_STAR: deinit_star((void*)b); break;
			default: PSHINE_PANIC("bad b->type: %d", (int)b->type); break;
		}
#define FREE_IF_NOTNULL(X) if ((X) != nullptr) free((X))
		FREE_IF_NOTNULL(b->surface.albedo_texture_path_own);
		FREE_IF_NOTNULL(b->surface.spec_texture_path_own);
		FREE_IF_NOTNULL(b->surface.lights_texture_path_own);
		FREE_IF_NOTNULL(b->surface.bump_texture_path_own);
		FREE_IF_NOTNULL(b->name_own);
		FREE_IF_NOTNULL(b->desc_own);
		FREE_IF_NOTNULL(b->tmp_parent_ref_name_own);
		FREE_IF_NOTNULL(b->orbit.cached_points_own);
#undef FREE_IF_NOTNULL
		free(b);
	}
	free(system->bodies_own);
	free(system->name_own);
}

void pshine_deinit_game(struct pshine_game *game) {
	eximgui_state_deinit(&game->data_own->eximgui_state);
	for (size_t i = 0; i < game->star_system_count; ++i) {
		deinit_star_system(game, &game->star_systems_own[i]);
	}
	free(game->star_systems_own);
	free(game->data_own);
}

static const enum pshine_key
	K_FORWARD = PSHINE_KEY_W,
	K_BACKWARD = PSHINE_KEY_S,
	K_UP = PSHINE_KEY_Q,
	K_DOWN = PSHINE_KEY_E,
	K_RIGHT = PSHINE_KEY_D,
	K_LEFT = PSHINE_KEY_A,
	K_ZOOM_IN = PSHINE_KEY_Z,
	K_ZOOM_OUT = PSHINE_KEY_X;

static const double ROTATE_SPEED = 1.0;

[[maybe_unused]]
static void update_camera_walk(struct pshine_game *game, float delta_time) {
	{
		double3 delta = {};
		if (pshine_is_key_down(game->renderer, PSHINE_KEY_LEFT)) delta.x += 1.0;
		else if (pshine_is_key_down(game->renderer, PSHINE_KEY_RIGHT)) delta.x -= 1.0;
		if (pshine_is_key_down(game->renderer, PSHINE_KEY_UP)) delta.y += 1.0;
		else if (pshine_is_key_down(game->renderer, PSHINE_KEY_DOWN)) delta.y -= 1.0;
		double rot_speed = ROTATE_SPEED * (game->data_own->is_control_precise ? 0.01 : 1.0);
		game->data_own->camera_pitch += delta.y * rot_speed * delta_time;
		game->data_own->camera_yaw += delta.x * rot_speed * delta_time;
	}

	// Unlike the Fly mode, we need to select a proper basis for our pitch/yaw rotation.
	// One of the axes is easy, the normal of the planet sphere, the other one a bit more complicated.
	// (we can get the third axis by doing a cross product of the other axes)

	double3x3 mat = {};
	setdouble3x3rotation(&mat, 0.0, game->data_own->camera_pitch, 0.0);
	{
		double3x3 mat2 = {};
		setdouble3x3rotation(&mat2,  game->data_own->camera_yaw, 0.0, 0.0);
		double3x3mul(&mat, &mat2);
	}


	double3 cam_forward = double3x3mulv(&mat, double3xyz(0.0, 0.0, 1.0));
	// double3 cam_forward = double3xyz(
	// 	cos(game->data_own->camera_pitch) * sin(game->data_own->camera_yaw),
	// 	-sin(game->data_own->camera_pitch),
	// 	cos(game->data_own->camera_pitch) * cos(game->data_own->camera_yaw)
	// );

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
static void update_camera_fly(struct pshine_game *game, float delta_time) {
	{
		double3 delta = {};
		if (pshine_is_key_down(game->renderer, PSHINE_KEY_LEFT)) delta.x += 1.0;
		else if (pshine_is_key_down(game->renderer, PSHINE_KEY_RIGHT)) delta.x -= 1.0;
		if (pshine_is_key_down(game->renderer, PSHINE_KEY_UP)) delta.y += 1.0;
		else if (pshine_is_key_down(game->renderer, PSHINE_KEY_DOWN)) delta.y -= 1.0;
		double rot_speed = ROTATE_SPEED * (game->data_own->is_control_precise ? 0.01 : 1.0);
		game->data_own->camera_pitch += delta.y * rot_speed * delta_time;
		game->data_own->camera_yaw += delta.x * rot_speed * delta_time;
	}

	double3x3 mat = {};
	setdouble3x3rotation(&mat, 0.0, game->data_own->camera_pitch, 0.0);
	{
		double3x3 mat2 = {};
		setdouble3x3rotation(&mat2,  game->data_own->camera_yaw, 0.0, 0.0);
		double3x3mul(&mat, &mat2);
	}

	//   0   -sina cosa
	//  cosb   0   sinb    0        cosasinb
	//   0     1    0    -sina  =    -sina
	// -sinb   0   cosb   cosa      cosacosb

	double3 cam_forward = double3x3mulv(&mat, double3xyz(0.0, 0.0, 1.0));
	// double3 cam_forward = double3xyz(
	// 	cos(game->data_own->camera_pitch) * sin(game->data_own->camera_yaw),
	// 	-sin(game->data_own->camera_pitch),
	// 	cos(game->data_own->camera_pitch) * cos(game->data_own->camera_yaw)
	// );

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

	double rot_speed = ROTATE_SPEED * (game->data_own->is_control_precise ? 0.01 : 1.0);
	game->data_own->camera_pitch += delta.y * rot_speed * delta_time;
	game->data_own->camera_yaw += delta.x * rot_speed * delta_time;

	game->data_own->camera_pitch = clampd(game->data_own->camera_pitch, -π/2 + 0.1, π/2 - 0.1);

	double3 cam_pos = double3mul(double3xyz(
		cos(game->data_own->camera_pitch) * sin(game->data_own->camera_yaw),
		sin(game->data_own->camera_pitch),
		-cos(game->data_own->camera_pitch) * cos(game->data_own->camera_yaw)
	), game->data_own->camera_dist);

	double3 b_pos = double3vs(game->star_systems_own[game->current_star_system].bodies_own[game->data_own->selected_body]->position.values);
	cam_pos = double3add(cam_pos, b_pos);
	double3 cam_forward = double3norm(double3sub(b_pos, cam_pos));
	// double3 cam_forward = double3xyz(-1.0f, 0.0f, 0.0f);

	*(double3*)game->camera_position.values = cam_pos;
	*(double3*)game->camera_forward.values = cam_forward;
}

static void propagate_orbit(
	double time,
	double gravitational_parameter,
	struct pshine_orbit_info *orbit
) {
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

	// double Δt = delta_time; // Change in time.
	double μ = gravitational_parameter;
	double a = orbit->semimajor; // The semimajor axis.
	double e = orbit->eccentricity; // The eccentricity.

	// Solve for χ using Newton's Method:
	// game->time += Δt; // TODO: figure out t from the orbital params.
	// game->time = fmod(game->time, T);
	double tsqrtμ = time * sqrt(μ);
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

// TODO: put parent_ref in pshine_orbit_info.

/// returns only the position for now.
static double3 kepler_orbit_to_state_vector(
	const struct pshine_orbit_info *orbit
) {
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
	//        r = Rrₚ, where R = R₁R₂R₃.
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
	R1.v3s[0] = double3xyz( cos(-ω), 0.0, sin(-ω));
	R1.v3s[1] = double3xyz(     0.0, 1.0,     0.0);
	R1.v3s[2] = double3xyz(-sin(-ω), 0.0, cos(-ω));
	double3x3 R2;
	R2.v3s[0] = double3xyz(1.0,     0.0,      0.0);
	R2.v3s[1] = double3xyz(0.0, cos(-i), -sin(-i));
	R2.v3s[2] = double3xyz(0.0, sin(-i),  cos(-i));
	double3x3 R3;
	R3.v3s[0] = double3xyz( cos(-Ω), 0.0, sin(-Ω));
	R3.v3s[1] = double3xyz(     0.0, 1.0,     0.0);
	R3.v3s[2] = double3xyz(-sin(-Ω), 0.0, cos(-Ω));

	double3x3 R = R2;
	double3x3mul(&R, &R1);
	double3x3mul(&R, &R3);

	double3 r = double3x3mulv(&R, rₚ);
	// r = double3add(r, double3vs(parent_ref->position.values));

	return r;
}

static void update_celestial_body(struct pshine_game *game, float delta_time, struct pshine_celestial_body *body) {
	if (!body->is_static) {
		propagate_orbit(game->time, body->parent_ref->gravitational_parameter, &body->orbit);
		body->rotation += body->rotation_speed * delta_time;
		double3 position = kepler_orbit_to_state_vector(&body->orbit);
		position = double3add(position, double3vs(body->parent_ref->position.values));
		*(double3*)&body->position = position;
	}
}

static inline ImVec4 rgbint_to_vec4(int r, int g, int b, int a) {
	return (ImVec4){ r / 255.f, g / 255.f, b / 255.f, a / 255.f };
}

static struct eximgui_state *g__eximgui_state = nullptr;

static void init_imgui_style() {
	ImGuiStyle *st = ImGui_GetStyle();
	st->Colors[ImGuiCol_WindowBg] = rgbint_to_vec4(0x02, 0x02, 0x02, 0xFE);
	st->Colors[ImGuiCol_Button] = rgbint_to_vec4(0x0C, 0x0C, 0x0C, 0xFF);
	st->Colors[ImGuiCol_ButtonHovered] = rgbint_to_vec4(0x0E, 0x0E, 0x0E, 0xFF);
	st->Colors[ImGuiCol_ButtonActive] = rgbint_to_vec4(0x0A, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_FrameBg] = rgbint_to_vec4(0x0C, 0x0C, 0x0C, 0xFF);
	st->Colors[ImGuiCol_FrameBgHovered] = rgbint_to_vec4(0x0E, 0x0E, 0x0E, 0xFF);
	st->Colors[ImGuiCol_FrameBgActive] = rgbint_to_vec4(0x0A, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_SliderGrab] = rgbint_to_vec4(0x30, 0x30, 0x30, 0xFF);
	st->Colors[ImGuiCol_SliderGrabActive] = rgbint_to_vec4(0x2C, 0x2C, 0x2C, 0xFF);
	st->Colors[ImGuiCol_Tab] = rgbint_to_vec4(0x05, 0x05, 0x05, 0xFF);
	st->Colors[ImGuiCol_TabActive] = rgbint_to_vec4(0x05, 0x05, 0x05, 0xFF);
	st->Colors[ImGuiCol_TabHovered] = rgbint_to_vec4(0x05, 0x05, 0x05, 0xFF);
	st->Colors[ImGuiCol_TabSelectedOverline] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_TabDimmedSelectedOverline] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_TabDimmedSelected] = rgbint_to_vec4(0x05, 0x05, 0x05, 0xFF);
	st->Colors[ImGuiCol_TabDimmed] = rgbint_to_vec4(0x05, 0x05, 0x05, 0xFF);
	st->Colors[ImGuiCol_TitleBg] = rgbint_to_vec4(0x05, 0x05, 0x05, 0xFF);
	st->Colors[ImGuiCol_TitleBgActive] = rgbint_to_vec4(0x0A, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_TitleBgCollapsed] = rgbint_to_vec4(0, 0, 0, 0xE0);
	st->Colors[ImGuiCol_TextSelectedBg] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_Header] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_HeaderActive] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_HeaderHovered] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_ResizeGrip] = rgbint_to_vec4(0x0C, 0x0C, 0x0C, 0xFF);
	st->Colors[ImGuiCol_ResizeGripActive] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_ResizeGripHovered] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_BorderShadow] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_SeparatorHovered] = rgbint_to_vec4(0x1F, 0x11, 0x11, 0xFF);
	st->Colors[ImGuiCol_SeparatorActive] = rgbint_to_vec4(0x1F, 0x11, 0x11, 0xFF);
	st->Colors[ImGuiCol_DockingEmptyBg] = rgbint_to_vec4(0x1F, 0x11, 0x11, 0x00);
	// st->FrameRounding = 3.0f;
	// st->Colors[ImGuiCol_Button] 31478479.308u
}

void pshine_post_init_game(struct pshine_game *game) {
	ImGuiIO *io = ImGui_GetIO();
	// ImFontConfig cfg = {};
	static const ImWchar ranges[] =
	{
		0x0020, 0x00FF, // Basic Latin + Latin Supplement
		0x2070, 0x209F, // Superscripts and Subscripts
		0,
	};
	float base_font_size = 14.f;
	g__eximgui_state->font_regular =
		ImFontAtlas_AddFontFromFileTTF(io->Fonts, "data/fonts/inter/Inter-Regular.ttf", base_font_size, nullptr, ranges);
	g__eximgui_state->font_italic =
		ImFontAtlas_AddFontFromFileTTF(io->Fonts, "data/fonts/inter/Inter-Italic.ttf", base_font_size, nullptr, ranges);
	g__eximgui_state->font_bold =
		ImFontAtlas_AddFontFromFileTTF(io->Fonts, "data/fonts/inter/Inter-Bold.ttf", base_font_size, nullptr, ranges);
	g__eximgui_state->font_title =
		ImFontAtlas_AddFontFromFileTTF(io->Fonts, "data/fonts/inter/Inter-Bold.ttf", 24.0f, nullptr, ranges);
	init_imgui_style();
}

static void eximgui_begin_frame() {
	ImGui_DockSpaceOverViewportEx(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode, nullptr);
	ImGui_PushFont(g__eximgui_state->font_regular);
}

static void eximgui_end_frame() {
	ImGui_PopFont();
}

static void eximgui_state_init(struct eximgui_state *st) {
	ImVector_Construct(&st->storage);
	g__eximgui_state = st;
}

static void eximgui_state_deinit(struct eximgui_state *st) {
	ImVector_Destruct(&st->storage);
}

static bool eximgui_begin_input_box(const char *label, const char *tooltip) {
	ImGuiID id = ImGui_GetID(label);
	bool is_open = ImGuiStorage_GetBool(&g__eximgui_state->storage, id, true);

	ImGui_PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0);
	ImGui_PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0);
	ImGui_PushStyleVar(ImGuiStyleVar_SeparatorTextBorderSize, 1.0);
	ImGui_PushStyleColor(ImGuiCol_ChildBg, 0xFF050505);
	bool r = ImGui_BeginChild(label, (ImVec2){}, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Border, 0);
	ImGui_SetCursorPosY(ImGui_GetCursorPosY() - 5.0f);
	ImGui_SeparatorText(label);
	ImGui_SetItemTooltip("%s", tooltip);
	if (ImGui_IsItemClicked()) {
		is_open = !is_open;
		ImGuiStorage_SetBool(&g__eximgui_state->storage, id, is_open);
	}
	ImGui_PopStyleColor();
	ImGui_PopStyleVar();
	ImGui_PopStyleVar();
	ImGui_PopStyleVar();
	// if (!is_open) {
	// 	ImVec2 win_size = ImGui_GetWindowSize();
	// 	win_size.y -= 10.0f;
	// 	ImGui_SetWindowSize(win_size, ImGuiCond_Always);
	// }
	// ImGui_PopStyleVar();
	return r && is_open;
}

static void eximgui_end_input_box() {
	ImGui_EndChild();
}

/// NB: always start label with '##'.
static bool eximgui_input_double3(const char *label, const char *tooltip, double *vs, double step, const char *format) {
	bool res = false;
	if (eximgui_begin_input_box(label + 2, tooltip)) {
		ImGui_PushIDInt(0);
		ImGui_Text("X"); ImGui_SameLine();
		res |= ImGui_InputDoubleEx(label, &vs[0], step, step * 100.0, format, 0);
		ImGui_PopID();
		ImGui_PushIDInt(1);
		ImGui_Text("Y"); ImGui_SameLine();
		res |= ImGui_InputDoubleEx(label, &vs[1], step, step * 100.0, format, 0);
		ImGui_PopID();
		ImGui_PushIDInt(2);
		ImGui_Text("Z"); ImGui_SameLine();
		res |= ImGui_InputDoubleEx(label, &vs[2], step, step * 100.0, format, 0);
		ImGui_PopID();
	}
	eximgui_end_input_box();
	return res;
}

static void update_star_system(struct pshine_game *game, struct pshine_star_system *system, float delta_time) {
	for (size_t i = 0; i < system->body_count; ++i) {
		update_celestial_body(game, delta_time, system->bodies_own[i]);
	}
}

struct eximgui_plot_info {
	float x_min, x_max;
	size_t x_ticks, y_ticks;
	size_t y_count;
	const float *ys;
	bool auto_y_range;
	bool extend_by_auto_range;
	float y_min, y_max;
};

static void eximgui_plot(const struct eximgui_plot_info *info) {
	const float x_min = info->x_min;
	const float x_max = info->x_max;
	const float x_d = x_max - x_min;

	float y_min = float_pinfty, y_max = float_ninfty;
	if (!info->auto_y_range) {
		y_min = info->y_min;
		y_max = info->y_max;
	}
	if (info->auto_y_range || info->extend_by_auto_range) {
		for (size_t i = 0; i < info->y_count; ++i) {
			if (info->ys[i] >= y_max) y_max = info->ys[i];
			if (info->ys[i] <= y_min) y_min = info->ys[i];
		}
	}
	const float y_d = y_max - y_min;

	const ImGuiStyle *style = ImGui_GetStyle();
	const ImVec2 cursor_pos = ImGui_GetCursorScreenPos();

	const float left_margin = 50.0f;

	const ImVec2 off = {
		cursor_pos.x + style->ItemSpacing.x + left_margin,
		cursor_pos.y + style->ItemSpacing.y,
	};
	const ImVec2 size = {
		ImGui_GetContentRegionAvail().x - 2.0 * style->ItemSpacing.x - left_margin,
		200.0f - 2.0 * style->ItemSpacing.y,
	};

	ImVec2 line_pts[info->y_count];
	float pt_step_x = size.x / info->y_count;
	// float pt_scale_x = size.x / x_d;
	float pt_scale_y = size.y / y_d;
	for (size_t i = 0; i < info->y_count; ++i) {
		line_pts[i].x = off.x + pt_step_x * i;
		line_pts[i].y = off.y + size.y - (info->ys[i] - y_min) * pt_scale_y;
	}

	ImDrawList *drawlist = ImGui_GetWindowDrawList();

	{
		size_t i = 0;
		const float Δx = x_d / info->x_ticks;
		for (float x = x_min; x < x_max; x += Δx, ++i) {
			const float gui_x = off.x + size.x * (x - x_min) / x_d;
			ImDrawList_AddLine(drawlist, (ImVec2){ gui_x, off.y }, (ImVec2){ gui_x, off.y + size.y },
				i % 4 == 0 ? 0x10FFFFFF : 0x05FFFFFF);
			if (i % 8 == 0) {
				char s[32] = {};
				snprintf(s, sizeof s, "%g", x);
				ImDrawList_AddText(drawlist, (ImVec2){
					gui_x,
					off.y + size.y + style->ItemInnerSpacing.y
				}, 0x10FFFFFF, s);
			}
		}
		i = 0;
		const float Δy = y_d / info->y_ticks;
		for (
			float y = y_min;
			y < y_max;
			y += Δy, ++i
		) {
			const float gui_y = off.y + size.y - size.y * (y - y_min) / y_d;
			ImDrawList_AddLine(drawlist, (ImVec2){ off.x, gui_y }, (ImVec2){ off.x + size.x, gui_y },
				i % 2 == 0 ? 0x10FFFFFF : 0x05FFFFFF);
			if (i % 8 == 0) {
				char s[32] = {};
				snprintf(s, sizeof s, "%.3g", y);
				ImVec2 text_size = ImGui_CalcTextSize(s);
				ImDrawList_AddText(drawlist, (ImVec2){
					off.x - text_size.x - style->ItemInnerSpacing.x,
					gui_y - text_size.y * 0.5
				}, 0x10FFFFFF, s);
			}
		}
	}

	ImDrawList_PushClipRect(drawlist, off,
		(ImVec2){ off.x + size.x, off.y + size.y }, false);
	ImDrawList_AddPolyline(drawlist, line_pts, info->y_count, 0xFFFFFFFF, ImDrawFlags_None, 2.0f);
	// ImDrawList_AddPolyline(drawlist, old_plot_positions, info->y_count, 0x02FFFFFF, ImDrawFlags_None, 1.0f);
	ImDrawList_PopClipRect(drawlist);

}

static void spectrometry_gui(struct pshine_game *game, float actual_delta_time) {
	if (ImGui_Begin("Spectrometry", nullptr, 0)) {
		struct pshine_celestial_body *star_body = game->star_systems_own[game->current_star_system].bodies_own[0];
		if (star_body->type != PSHINE_CELESTIAL_BODY_STAR) {
			ImGui_PushStyleColor(ImGuiCol_Text, ImGui_ColorConvertFloat4ToU32((ImVec4){1.0, 0.2, 0.2, 1.0}));
			ImGui_Text("Configuration error: the first specified celestial body must be the star.");
			ImGui_Text("Multi-star system support TBD.");
			ImGui_PopStyleColor();
			ImGui_End();
			return;
		}

		struct pshine_star *star = (void*)star_body;
		double dist2 = double3mag2(double3sub(
			double3mul(double3vs(game->camera_position.values), PSHINE_XCS_WCS_FACTOR),
			double3mul(double3vs(star_body->position.values), PSHINE_XCS_WCS_FACTOR)
		));

		float temp = star->temperature;
		// ImGui_SliderFloat("Temp, K", &temp, 1200, 8000);

		enum : size_t { SAMPLE_COUNT = 50 };
		float values[SAMPLE_COUNT];
		const float λ_min = 240.0f;
		const float λ_max = 1840.0f; // 2.8977721e-3 / temp * 1e9;
		const float λ_step = (λ_max - λ_min) / SAMPLE_COUNT;

		for (size_t i = 0; i < SAMPLE_COUNT; ++i) {
			values[i] = (double)pshine_blackbody_radiation(λ_min + λ_step * i, temp) / dist2;
			// introduce some noise
			// values[i] += ((double)pshine_pcg64_random_float(&game->rng64) * 2.0 - 1.0) * 0.0001 / sqrt(dist2);
			// values[i] += (pshine_pcg64_random_float(&game->rng64) * 2.0 - 1.0) * 0.0001;
		}

		eximgui_plot(&(struct eximgui_plot_info){
			.y_count = SAMPLE_COUNT,
			.ys = values,
			.x_min = λ_min,
			.x_max = λ_max,
			.x_ticks = 30,
			.y_ticks = 30,
			.auto_y_range = true,
			.extend_by_auto_range = true,
			.y_min = -0.0004,
			.y_max = 0.05,
		});

		// double value_norm_min = -0.02f;
		// double value_norm_max = 1.0f;
		// double value_max = -9999.99f;
		// {
		// }

		// ImGui_Text("%f", value_max);

		// const ImGuiStyle *style = ImGui_GetStyle();
		// const ImVec2 cursor_pos = ImGui_GetCursorScreenPos();
		// const ImVec2 plot_off = {
		// 	cursor_pos.x + style->ItemSpacing.x + 50.0f,
		// 	cursor_pos.y + style->ItemSpacing.y,
		// };
		// const ImVec2 plot_size = {
		// 	ImGui_GetContentRegionAvail().x - 2.0 * style->ItemSpacing.x - 50.0f,
		// 	200.0f - 2.0 * style->ItemSpacing.y,
		// };

		// static ImVec2 old_plot_positions[SAMPLE_COUNT];
		// static ImVec2 plot_positions[SAMPLE_COUNT];

		// float pt_step_x = plot_size.x / SAMPLE_COUNT;
		// float pt_scale_y = plot_size.y / (value_norm_max - value_norm_min);
		// {
		// 	for (size_t i = 0; i < SAMPLE_COUNT; ++i) {
		// 		plot_positions[i].x = plot_off.x + pt_step_x * i;
		// 		plot_positions[i].y = plot_off.y + plot_size.y - (values[i] / value_max - value_norm_min) * pt_scale_y;
		// 	}
		// }

		// ImDrawList *drawlist = ImGui_GetWindowDrawList();

		// {
		// 	size_t i = 0;
		// 	for (float λ = λ_min; λ < λ_max; (λ += 10.0f), ++i) {
		// 		const float x = plot_off.x + plot_size.x * ((λ - λ_min) / (λ_max - λ_min));
		// 		ImDrawList_AddLine(drawlist, (ImVec2){ x, plot_off.y }, (ImVec2){ x, plot_off.y + plot_size.y },
		// 			i % 4 == 0 ? 0x10FFFFFF : 0x05FFFFFF);
		// 		if (i % 8 == 0) {
		// 			char s[32] = {};
		// 			snprintf(s, sizeof s, "%g", λ);
		// 			ImDrawList_AddText(drawlist, (ImVec2){
		// 				x,
		// 				plot_off.y + plot_size.y + style->ItemInnerSpacing.y
		// 			}, 0x10FFFFFF, s);
		// 		}
		// 	}
		// 	i = 0;
		// 	for (
		// 		float l = value_norm_min * value_max;
		// 		l < value_max;
		// 		(l += 2.0f * (value_max - value_min) / 50.0f), ++i
		// 	) {
		// 		ImDrawList_AddLine(drawlist, (ImVec2){
		// 			plot_off.x,
		// 			plot_off.y + plot_size.y - (l / value_max - value_norm_min) * pt_scale_y,
		// 		}, (ImVec2){
		// 			plot_off.x + plot_size.x,
		// 			plot_off.y + plot_size.y - (l / value_max - value_norm_max) * pt_scale_y,
		// 		}, i % 2 == 0 ? 0x10FFFFFF : 0x05FFFFFF);
		// 		if (i % 8 == 0) {
		// 			char s[32] = {};
		// 			snprintf(s, sizeof s, "%.3g", l - value_norm_min);
		// 			ImVec2 text_size = ImGui_CalcTextSize(s);
		// 			ImDrawList_AddText(drawlist, (ImVec2){
		// 				plot_off.x - text_size.x - style->ItemInnerSpacing.x,
		// 				plot_off.y + plot_size.y - l * pt_scale_y - text_size.y * 0.5
		// 			}, 0x10FFFFFF, s);
		// 		}
		// 	}
		// }

		// ImDrawList_PushClipRect(drawlist, plot_off,
		// 	(ImVec2){ plot_off.x + plot_size.x, plot_off.y + plot_size.y }, false);
		// ImDrawList_AddPolyline(drawlist, plot_positions, SAMPLE_COUNT, 0xFFFFFFFF, ImDrawFlags_None, 2.0f);
		// ImDrawList_AddPolyline(drawlist, old_plot_positions, SAMPLE_COUNT, 0x02FFFFFF, ImDrawFlags_None, 1.0f);
		// ImDrawList_PopClipRect(drawlist);

		// for (size_t i = 0; i < SAMPLE_COUNT; ++i) {
		// 	old_plot_positions[i] = plot_positions[i];
		// }
	}
	ImGui_End();
}

static void science_gui(struct pshine_game *game, float actual_delta_time) {
	spectrometry_gui(game, actual_delta_time);

	if (ImGui_Begin("Gravity", nullptr, 0)) {
		ImGui_PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0);
		ImGui_PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0);
		ImGui_PushStyleColor(ImGuiCol_ChildBg, 0xFF050505);
		ImVec2 size = { 100, 100 };
		if (ImGui_BeginChild("Superposition", size, ImGuiChildFlags_Border, 0)) {
			double3 sum = double3v0();
			for (size_t i = 0; i < game->star_systems_own[game->current_star_system].body_count; ++i) {
				struct pshine_celestial_body *body = game->star_systems_own[game->current_star_system].bodies_own[i];
				double3 d = double3sub(
					double3mul(double3vs(game->camera_position.values), PSHINE_XCS_WCS_FACTOR),
					double3mul(double3vs(body->position.values), PSHINE_XCS_WCS_FACTOR)
				);
				sum = double3add(sum, double3div(d, double3mag2(d) / body->gravitational_parameter));
			}
			float3 dir = float3_double3(double3norm(sum));
			ImDrawList *drawlist = ImGui_GetWindowDrawList();
			float radius = 40.0f;
			ImGuiStyle *style = ImGui_GetStyle();
			ImVec2 off = ImGui_GetCursorScreenPos();
			off.x -= style->WindowPadding.x;
			off.y -= style->WindowPadding.y;
			ImVec2 center = { off.x + size.x / 2, off.y + size.y / 2 };
			ImDrawList_AddCircle(
				drawlist,
				center,
				radius,
				0x10FFFFFF
			);


			float3x3 mat = {};
			setfloat3x3rotation(&mat, 0.0, -game->data_own->camera_pitch, 0.0);
			{
				float3x3 mat2 = {};
				setfloat3x3rotation(&mat2, -game->data_own->camera_yaw, 0.0, 0.0);
				float3x3mul(&mat, &mat2);
			}
			dir = float3x3mulv(&mat, dir);
			float2 pdir = float2xy(-dir.x, dir.y);

			float2 arrow_end = float2xy(pdir.x * radius, pdir.y * radius);
			float2 arrow_end_n = float2norm(arrow_end);

			ImDrawList_AddLine(
				drawlist,
				center,
				(ImVec2){ center.x + arrow_end.x, center.y + arrow_end.y },
				0xFF0000FF
			);
			ImDrawList_AddLine(
				drawlist,
				(ImVec2){ center.x + arrow_end.x, center.y + arrow_end.y },
				(ImVec2){
					center.x + arrow_end.x + arrow_end_n.y * 5 - arrow_end_n.x * 5,
					center.y + arrow_end.y - arrow_end_n.x * 5 - arrow_end_n.y * 5,
				},
				0xFF0000FF
			);
			ImDrawList_AddLine(
				drawlist,
				(ImVec2){ center.x + arrow_end.x, center.y + arrow_end.y },
				(ImVec2){
					center.x + arrow_end.x - arrow_end_n.y * 5 - arrow_end_n.x * 5,
					center.y + arrow_end.y + arrow_end_n.x * 5 - arrow_end_n.y * 5,
				},
				0xFF0000FF
			);
		}
		ImGui_EndChild();
		ImGui_PopStyleColor();
		ImGui_PopStyleVar();
		ImGui_PopStyleVar();
	}
	ImGui_End();
}

void pshine_update_game(struct pshine_game *game, float actual_delta_time) {
	game->time += actual_delta_time * game->time_scale;
	for (size_t i = 0; i < game->star_system_count; ++i) {
		update_star_system(game, &game->star_systems_own[i], actual_delta_time * game->time_scale);
	}

	if (pshine_is_key_down(game->renderer, PSHINE_KEY_P) && !game->data_own->last_key_states[PSHINE_KEY_P]) {
		game->data_own->is_control_precise = !game->data_own->is_control_precise;
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


	switch (game->data_own->movement_mode) {
		case MOVEMENT_ARC: update_camera_arc(game, actual_delta_time); break;
		case MOVEMENT_FLY: update_camera_fly(game, actual_delta_time); break;
		case MOVEMENT_WALK: update_camera_walk(game, actual_delta_time); break;
		default:
			PSHINE_WARN("Unknown movement mode: %d, switching to fly", game->data_own->movement_mode);
			game->data_own->movement_mode = MOVEMENT_FLY;
			break;
	}

	memcpy(game->data_own->last_key_states, pshine_get_key_states(game->renderer), sizeof(uint8_t) * PSHINE_KEY_COUNT_);

#define SCSd3_WCSd3(wcs) (double3mul((wcs), PSHINE_SCS_FACTOR))
#define SCSd3_WCSp3(wcs) SCSd3_WCSd3(double3vs((wcs).values))
#define SCSd_WCSd(wcs) ((wcs) * PSHINE_SCS_FACTOR)

	if (!game->ui_dont_render_windows) {
		eximgui_begin_frame();

		if (ImGui_Begin("Material", nullptr, 0)) {
			ImGui_DragFloat("Smoothness", &game->material_smoothness_);
		}
		ImGui_End();

		if (ImGui_Begin("Star Systems", nullptr, 0)) {
			for (size_t i = 0; i < game->star_system_count; ++i) {
				bool selected = i == game->current_star_system;
				if (ImGui_SelectableBoolPtr(game->star_systems_own[i].name_own, &selected, ImGuiSelectableFlags_None)) {
					game->current_star_system = i;
					if (game->star_systems_own[game->current_star_system].body_count <= game->data_own->selected_body) {
						game->data_own->selected_body = game->star_systems_own[game->current_star_system].body_count;
					}
				}
			}
		}
		ImGui_End();

		struct pshine_star_system *current_system = &game->star_systems_own[game->current_star_system];
		if (ImGui_Begin("Celestial Bodies", nullptr, 0)) {
			for (size_t i = 0; i < current_system->body_count; ++i) {
				bool selected = i == game->data_own->selected_body;
				if (ImGui_SelectableBoolPtr(current_system->bodies_own[i]->name_own, &selected, ImGuiSelectableFlags_None)) {
					game->data_own->selected_body = i;
				}
			}
		}
		ImGui_End();

		struct pshine_celestial_body *body = current_system->bodies_own[game->data_own->selected_body];

		if (ImGui_Begin("Orbit", NULL, 0)) {
			if (!body->is_static) {
				ImGui_Text("True anomaly: %.5f", body->orbit.true_anomaly);
				double3 pos = (SCSd3_WCSp3(body->position));
				ImGui_Text("Position (SCS): %.0f, %.0f, %.0f", pos.x, pos.y, pos.z);
				double μ = body->parent_ref->gravitational_parameter;
				double a = body->orbit.semimajor; // The semimajor axis.
				double e = body->orbit.eccentricity; // The eccentricity.

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
			} else {
				ImGui_PushStyleColor(ImGuiCol_Text, 0xff747474);
				ImGui_Text("This body doesn't have an orbit.");
				ImGui_PopStyleColor();
			}
		}
		ImGui_End();

		if (ImGui_Begin("Camera", NULL, 0)) {
			double3 p = double3vs(game->camera_position.values);
			double3 p_scs = double3mul(p, PSHINE_SCS_FACTOR);
			double d = double3mag(double3sub(p, double3vs(body->position.values))) - body->radius;
			enum si_prefix d_prefix = find_optimal_si_prefix(d);
			double d_scaled = apply_si_prefix(d_prefix, d);
			eximgui_input_double3("##WCS Position", "World coordinate system camera position. In meters.",
				game->camera_position.values, 1000.0, "%.3fm");
			if (eximgui_input_double3("##SCS Position", "Scaled coordinate system camera position. 1:8192.",
				p_scs.vs, 100.0, "%.3fu")) {
				*(double3*)&game->camera_position.values = double3mul(p_scs, PSHINE_SCS_SCALE);
			}
			if (eximgui_begin_input_box("Movement speed", "The speed at which the camera moves.")) {
				ImGui_Text("Speed, m/s");
				ImGui_SameLine();
				ImGui_InputDouble("##Movement speed", &game->data_own->move_speed);
				ImGui_Text("That's %0.3fc", game->data_own->move_speed / PSHINE_SPEED_OF_LIGHT);
				struct {
					const char *name;
					const char *tooltip;
					double value;
				} marks[] = {
					{ "Crawl", "100 m/s", 100.0 },
					{ "Snail", "1 km/s", 1000.0 },
					{ "Slow", "10 km/s", 10000.0 },
					{ "Fast", "500 km/s", 5.0e5 },
					{ "Faster", "50 Mm/s", 5.0e7 },
					{ "Light", "≈300 Mm/s", PSHINE_SPEED_OF_LIGHT },
					{ "10c", "10 times the speed of light", PSHINE_SPEED_OF_LIGHT * 10.0 },
					{ "166c", "≈166 times the speed of light", 5.0e10 },
					{ "1kc", "1000 times the speed of light", PSHINE_SPEED_OF_LIGHT * 1000.0 },
					{ "2kc", "2000 times the speed of light", PSHINE_SPEED_OF_LIGHT * 2000.0 },
				};
				for (size_t i = 0; i < sizeof(marks)/sizeof(*marks); ++i) {
					if (ImGui_Button(marks[i].name)) game->data_own->move_speed = marks[i].value;
					ImGui_SetItemTooltip("%s", marks[i].tooltip);
					ImGui_SameLine();
					if (
						i + 1 >= sizeof(marks)/sizeof(*marks) ||
						0
						+ ImGui_GetStyle()->ItemSpacing.x
						+ ImGui_GetStyle()->ItemInnerSpacing.x * 2.0
						+ ImGui_CalcTextSize(marks[i + 1].name).x
						>= ImGui_GetContentRegionAvail().x * 2.0
					) ImGui_NewLine();
				}
			}
			eximgui_end_input_box();
			ImGui_Text("Distance from surface: %.3f %s m = %.3fly", d_scaled, si_prefix_english(d_prefix), d / 9.4607e+15);
			ImGui_Text("Yaw: %.3frad, Pitch: %.3frad", game->data_own->camera_yaw, game->data_own->camera_pitch);
			if (ImGui_Button("Reset rotation")) {
				game->data_own->camera_yaw = 0.0;
				game->data_own->camera_pitch = 0.0;
			}
			ImGui_SetItemTooltip("Set the rotation to (0.0, 0.0)");
			ImGui_SameLine();
			if (ImGui_Button("Center on target")) {
				double3 bp_wcs = double3vs(body->position.values);
				double3 bp_scs = double3mul(bp_wcs, PSHINE_SCS_FACTOR);
				game->data_own->camera_yaw = π / 2.0 + atan2(
					p_scs.z - bp_scs.z,
					p_scs.x - bp_scs.x
				);
				// TODO: this is a bit weird.
				game->data_own->camera_pitch = -atan2(
					p_scs.y - bp_scs.y,
					p_scs.x - bp_scs.x
				);
				// game->data_own->camera_pitch = 0.0;
			}
			ImGui_SetItemTooltip("Set the rotation so that the selected body is in the center");
			ImGui_SameLine();
			ImGui_BeginDisabled(true);
			if (ImGui_Button("Reset position")) {
				game->data_own->camera_yaw = 0.0;
				game->data_own->camera_pitch = 0.0;
			}
			ImGui_EndDisabled();
			if (eximgui_begin_input_box("Graphics", "Graphics settings.")) {
				ImGui_SliderFloat("FoV", &game->graphics_settings.camera_fov, 0.00001f, 179.999f);
				ImGui_SliderFloat("EV", &game->graphics_settings.exposure, -8.0f, 6.0f);

				if (eximgui_begin_input_box("Bloom", "Bloom effect settings. TBD.")) {
					ImGui_SliderFloat("Knee", &game->graphics_settings.bloom_knee, 0.0f, 16.0f);
					ImGui_SliderFloat("Threshold", &game->graphics_settings.bloom_threshold, game->graphics_settings.bloom_knee, 16.0f);
				}
				eximgui_end_input_box();
			}
			eximgui_end_input_box();
			if (eximgui_begin_input_box("Presets", "Preset times, positions and rotations with good views")) {
				if (ImGui_Button("KJ63 system view##Preset_0")) {
					game->time = 126000.0;
					game->current_star_system = 0;
					game->data_own->camera_yaw = 6.088;
					game->data_own->camera_pitch = -0.227;
					game->camera_position.xyz.x = -117766076861.631;
					game->camera_position.xyz.y = 159655341.412;
					game->camera_position.xyz.z = 69706944591.439;
				}
			}
			eximgui_end_input_box();

			// ImGui_Spacing();
			// ImVec2 begin = ImGui_GetCursorScreenPos();
			// ImVec2 size = ImGui_GetItemRectSize();
			// ImVec2 end = { begin.x + size.x, begin.y + size.y };
			// ImDrawList_AddRectFilledEx(ImDrawList *self, ImVec2 p_min, ImVec2 p_max, ImU32 col, float rounding, ImDrawFlags flags)
			// ImDrawList_AddRectFilledMultiColor(
			// 	ImGui_GetWindowDrawList(),
			// 	begin,
			// 	end,
			// 	0xFF565656,
			// 	0xFF565656,
			// 	0xFF232323,
			// 	0xFF232323
			// );
		}
		ImGui_End();

		if (body->type == PSHINE_CELESTIAL_BODY_PLANET) {
			if (ImGui_Begin("Planet", NULL, 0)) {
				struct pshine_planet *planet = (struct pshine_planet*)body;
				ImGui_BeginDisabled(!planet->has_atmosphere);
				if (eximgui_begin_input_box("Atmosphere", "The paramters of the planet's atmosphere. You may need to recalculate the LUT.")) {
					struct pshine_atmosphere_info *atmo = &planet->atmosphere;
					ImGui_SliderFloat3("Rayleigh Coefs.", atmo->rayleigh_coefs, 0.001f, 50.0f);
					ImGui_SetItemTooltip("The Rayleigh scattering coeffitients for Red, Green and Blue.");
					ImGui_SliderFloat("Rayleigh Falloff.", &atmo->rayleigh_falloff, 0.0001f, 100.0f);
					ImGui_SetItemTooltip("The falloff factor for Rayleigh scattering.");
					ImGui_SliderFloat("Mie Coef.", &atmo->mie_coef, 0.001f, 50.0f);
					ImGui_SetItemTooltip("The coefficient for Mie scattering.");
					ImGui_SliderFloat("Mie Ext. Coef.", &atmo->mie_ext_coef, 0.001f, 5.0f);
					ImGui_SetItemTooltip("The external light coefficient for Mie scattering.");
					ImGui_SliderFloat("Mie 'g' Coef.", &atmo->mie_g_coef, -0.9999f, 0.9999f);
					ImGui_SetItemTooltip("The 'g' coefficient for Mie scattering.");
					ImGui_SliderFloat("Mie Falloff.", &atmo->mie_falloff, 0.0001f, 100.0f);
					ImGui_SetItemTooltip("The falloff factor for Mie scattering.");
					ImGui_SliderFloat("Intensity", &atmo->intensity, 0.0f, 50.0f);
					ImGui_SetItemTooltip("The intensity of the atmosphere. Not really a physical property.");
				}
				eximgui_end_input_box();
				ImGui_EndDisabled();
				ImGui_BeginDisabled(!body->rings.has_rings);
				if (eximgui_begin_input_box("Rings", "Parameters of the planetary rings.")) {
					struct pshine_rings_info *rings = &body->rings;
					ImGui_SliderFloat("Shadow smoothing", &rings->shadow_smoothing, -1.0f, 1.0f);
					ImGui_SetItemTooltip("How \"smooth\" the transition to and from shadow is.");
				}
				eximgui_end_input_box();
				ImGui_EndDisabled();
			}
			ImGui_End();
		}

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

		ImGui_PushStyleColor(ImGuiCol_WindowBg, 0x00000030);
		ImGui_PushStyleColor(ImGuiCol_Border, 0x00000000);
		ImGui_PushStyleColor(ImGuiCol_BorderShadow, 0x00000000);
		if (ImGui_Begin("Selected celestial body", nullptr,
			// ImGuiWindowFlags_NoTitleBar |
			// ImGuiWindowFlags_NoResize |
			// ImGuiWindowFlags_NoScrollbar |
			// ImGuiWindowFlags_NoCollapse |
			// ImGuiWindowFlags_NoDocking |
			// ImGuiWindowFlags_AlwaysAutoResize |
			0
		)) {
			ImGui_PushFont(g__eximgui_state->font_title);
			ImGui_Text("%s", body->name_own);
			ImGui_PopFont();
			ImGui_TextWrapped("%s", body->desc_own != nullptr ? body->desc_own : "?");
			if (body->type == PSHINE_CELESTIAL_BODY_PLANET) {
				struct pshine_planet *planet = (void*)body;
				if (planet->has_atmosphere) {
					ImGui_PushFont(g__eximgui_state->font_bold);
					ImGui_Text("Atmospheric composition");
					ImGui_PopFont();
					if (ImGui_BeginTable("_AtmosphericCompositionTable", 2,
						ImGuiTableFlags_Borders |
						ImGuiTableFlags_RowBg
					)) {
						// ImGui_TableSetColumnIndex(0);
						ImGui_TableSetupColumn("Fraction", 0);
						// ImGui_TableSetColumnIndex(1);
						ImGui_TableSetupColumn("Element", 0);
						ImGui_TableHeadersRow();
						for (size_t i = 0; i < planet->atmosphere.composition.constituent_count; ++i) {
							struct pshine_named_constituent c = planet->atmosphere.composition.constituents_own[i];
							ImGui_TableNextRow();
							ImGui_TableSetColumnIndex(0);
							ImGui_Text("%g%%", c.fraction * 100.0f);
							ImGui_TableSetColumnIndex(1);
							ImGui_Text("%s", c.name_own);
						}
						ImGui_EndTable();
					}
				}
			}
		}
		ImGui_End();
		ImGui_PopStyleColor();
		ImGui_PopStyleColor();
		ImGui_PopStyleColor();

		if (ImGui_Begin("Blackbody Radiation Test", nullptr, 0)) {
			static float temp = 4200.0;
			ImGui_SliderFloat("Temp, K", &temp, 1200, 8000);
			pshine_color_rgb rgb = pshine_blackbody_temp_to_rgb(temp);
			ImGui_ColorEdit3("Color", rgb.values, ImGuiColorEditFlags_NoInputs
				| ImGuiColorEditFlags_NoPicker);
		}
		ImGui_End();

		science_gui(game, actual_delta_time);

		eximgui_end_frame();
	}
}
