#ifndef PSHINE_VERTEX_UTIL_H_
#define PSHINE_VERTEX_UTIL_H_
#include "math.h"

// The next couple of functions are from https://www.jeremyong.com/graphics/2023/01/09/tangent-spaces-and-diamond-encoding/

static inline float encode_diamond(float2 p) {
	// Project to the unit diamond, then to the x-axis.
	float x = p.x / (fabs(p.x) + fabs(p.y));

	// Contract the x coordinate by a factor of 4 to represent all 4 quadrants in
	// the unit range and remap
	float py_sign = copysign(1, p.y);
	return -py_sign * 0.25f * x + 0.5f + py_sign * 0.25f;
}

static inline float2 decode_diamond(float p) {
	float2 v;

	// Remap p to the appropriate segment on the diamond
	float p_sign = copysign(1, p - 0.5f);
	v.x = -p_sign * 4.f * p + 1.f + p_sign * 2.f;
	v.y = p_sign * (1.f - fabs(v.x));

	// Normalization extends the point on the diamond back to the unit circle
	return float2norm(v);
}

// Given a normal and tangent vector, encode the tangent as a single float that can be
// subsequently quantized.
static inline float encode_tangent(float3 normal, float3 tangent) {
	// First, find a canonical direction in the tangent plane
	float3 t1;
	if (fabs(normal.y) > fabs(normal.z)) {
		// Pick a canonical direction orthogonal to n with z = 0
		t1 = float3xyz(normal.y, -normal.x, 0.f);
	} else {
		// Pick a canonical direction orthogonal to n with y = 0
		t1 = float3xyz(normal.z, 0.f, -normal.x);
	}
	t1 = float3norm(t1);

	// Construct t2 such that t1 and t2 span the plane
	float3 t2 = float3cross(t1, normal);

	// Decompose the tangent into two coordinates in the canonical basis
	float2 packed_tangent = float2xy(float3dot(tangent, t1), float3dot(tangent, t2));

	// Apply our diamond encoding to our two coordinates
	return encode_diamond(packed_tangent);
}

static inline float3 decode_tangent(float3 normal, float diamond_tangent) {
	// As in the encode step, find our canonical tangent basis span(t1, t2)
	float3 t1;
	if (fabs(normal.y) > fabs(normal.z)) {
		t1 = float3xyz(normal.y, -normal.x, 0.f);
	} else {
		t1 = float3xyz(normal.z, 0.f, -normal.x);
	}
	t1 = float3norm(t1);

	float3 t2 = float3cross(t1, normal);

	// Recover the coordinates used with t1 and t2
	float2 packed_tangent = decode_diamond(diamond_tangent);

	return float3add(float3mul(t1, packed_tangent.x), float3mul(t2, packed_tangent.y));
}

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


#endif // PSHINE_VERTEX_UTIL_H_
