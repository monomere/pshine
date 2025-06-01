#include "game.h"
#include "../vertex_util.h"

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
		0.0f,
		{ nor_oct.x, nor_oct.y },
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
