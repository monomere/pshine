#ifndef PSHINE_IMPL_MATH_H_
#define PSHINE_IMPL_MATH_H_
#include "pshine/util.h"
#include <stddef.h>
#include <string.h>
#include <math.h>

typedef union {
	struct { float x, y; };
	struct { float r, g; };
	float vs[2];
} float2;

typedef union {
	struct { float x, y, z; };
	struct { float r, g, b; };
	float vs[3];
} float3;

typedef union {
	struct { float x, y, z, w; };
	struct { float r, g, b, a; };
	float vs[4];
} float4;

// represents a unit quaternion of the form `a + bi + cj + dk`
typedef union {
	struct { float a, b, c, d; };
	float vs[4];
} versor4;

typedef union {
	struct { float vs[4][4]; };
	struct { float4 v4s[4]; };
} float4x4;

static inline float2 float2xy(float x, float y) { return (float2){{ x, y }}; }

static inline float4 float4xyz3w(float3 xyz, float w) { return (float4){{ xyz.x, xyz.y, xyz.z, w }}; }
static inline float4 float4xyzw(float x, float y, float z, float w) { return (float4){{ x, y, z, w }}; }
static inline float4 float4rgba(float r, float g, float b, float a) { return (float4){{ r, g, b, a }}; }

static inline float3 float3vs(const float vs[3]) { return (float3){{ vs[0], vs[1], vs[2] }}; }
static inline float3 float3v0() { return (float3){{0}}; }
static inline float3 float3v(float v) { return (float3){{ v, v, v }}; }
static inline float3 float3xyz(float x, float y, float z) { return (float3){{ x, y, z }}; }
static inline float3 float3rgb(float r, float g, float b) { return (float3){{ r, g, b }}; }
static inline float3 float3neg(float3 v) { return float3xyz(-v.x, -v.y, -v.z); }
static inline float float3dot(float3 a, float3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline float float3mag2(float3 v) { return float3dot(v, v); }
static inline float float3mag(float3 v) { return sqrtf(float3mag2(v)); }
static inline float3 float3div(float3 v, float s) { return float3xyz(v.x/s, v.y/s, v.z/s); }
static inline float3 float3mul(float3 v, float s) { return float3xyz(v.x*s, v.y*s, v.z*s); }
static inline float3 float3add(float3 a, float3 b) { return float3xyz(a.x+b.x, a.y+b.y, a.z+b.z); }
static inline float3 float3sub(float3 a, float3 b) { return float3xyz(a.x-b.x, a.y-b.y, a.z-b.z); }
static inline float3 float3norm(float3 v) {
	float m = float3mag2(v);
	if (fabsf(m) <= 0.00001f) return (float3){};
	return float3div(v, sqrtf(m));
}
static inline float3 float3cross(float3 a, float3 b) {
	return float3xyz(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static inline versor4 quat4abcd(float a, float b, float c, float d) { return (versor4){{ a, b, c, d }}; }
static inline versor4 quat4mul(float a, float b, float c, float d) { return (versor4){{ a, b, c, d }}; }

static inline void setfloat4x4iden(float4x4 *m) {
	memset(m->vs, 0, sizeof(m->vs));
	m->vs[0][0] = 1.0f;
	m->vs[1][1] = 1.0f;
	m->vs[2][2] = 1.0f;
	m->vs[3][3] = 1.0f;
}

static inline void floata4mula(float o[4], const float v[4], float s) {
	o[0] += v[0] * s;
	o[1] += v[1] * s;
	o[2] += v[2] * s;
	o[3] += v[3] * s;
}

static inline void float4x4trans(float4x4 *m, float3 d) {
	float r[4] = {};
	floata4mula(r, m->vs[0], d.x);
	floata4mula(r, m->vs[1], d.y);
	floata4mula(r, m->vs[2], d.z);
	floata4mula(m->vs[3], r, 1);
}

static inline void float4x4scale(float4x4 *m, float3 s) {
	m->vs[0][0] *= s.x;
	m->vs[0][1] *= s.y;
	m->vs[0][2] *= s.z;
	m->vs[1][0] *= s.x;
	m->vs[1][1] *= s.y;
	m->vs[1][2] *= s.z;
	m->vs[2][0] *= s.x;
	m->vs[2][1] *= s.y;
	m->vs[2][2] *= s.z;
	m->vs[3][0] *= s.x;
	m->vs[3][1] *= s.y;
	m->vs[3][2] *= s.z;
}

struct float4x4persp_info {
	float2 plane;
	float znear;
};

#define π 3.14159265358979f

static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float clampf(float x, float a, float b) { return minf(maxf(x, a), b); }

static inline struct float4x4persp_info setfloat4x4persp_rhoz(
	float4x4 *m, float fov, float aspect, float znear, float zfar
) {
	// https://gist.github.com/pezcode/1609b61a1eedd207ec8c5acf6f94f53a
	memset(m->vs, 0, sizeof(m->vs));
	struct float4x4persp_info info;
	float t = tanf(fov * 0.5f * π / 180.0f);
	info.plane.y = t * znear;
	info.plane.x = info.plane.y * aspect;
	info.znear = znear;
	float k = znear / (znear - zfar);
	float g = 1.0f / t;
	m->vs[0][0] = g / aspect;
	m->vs[1][1] = -g;
	m->vs[2][2] = -k;
	m->vs[2][3] = 1.0f;
	m->vs[3][2] = -znear * k;

	return info;
}

static inline struct float4x4persp_info setfloat4x4persp_rhozi(
	float4x4 *m, float fov, float aspect, float znear
) {
	// http://www.songho.ca/opengl/gl_projectionmatrix.html#perspective
	// https://computergraphics.stackexchange.com/a/12453
	// https://discourse.nphysics.org/t/reversed-z-and-infinite-zfar-in-projections/341/2
	memset(m->vs, 0, sizeof(m->vs));
	struct float4x4persp_info info;
	float t = tanf(fov * 0.5f * π / 180.0f);
	info.plane.y = t * znear;
	info.plane.x = info.plane.y * aspect;
	info.znear = znear;
	float g = 1.0f / t;

	m->vs[0][0] = g / aspect;
	m->vs[1][1] = -g;
	m->vs[3][2] = znear;
	m->vs[2][3] = 1.0f;

	return info;
}

static inline struct float4x4persp_info setfloat4x4persp(float4x4 *m, float fov, float aspect, float znear) {
	// return setfloat4x4persp_rhoz(m, fov, aspect, znear, 1000.0f);
	return setfloat4x4persp_rhozi(m, fov, aspect, znear);
}

static inline void setfloat4x4lookat(float4x4 *m, float3 eye, float3 center, float3 up) {
	memset(m->vs, 0, sizeof(m->vs));
	float3 f = float3norm(float3sub(center, eye));
	float3 s = float3norm(float3cross(up, f));
	float3 u = float3cross(f, s);

	m->vs[0][0] = s.x;
	m->vs[1][0] = s.y;
	m->vs[2][0] = s.z;
	m->vs[0][1] = u.x;
	m->vs[1][1] = u.y;
	m->vs[2][1] = u.z;
	m->vs[0][2] = f.x;
	m->vs[1][2] = f.y;
	m->vs[2][2] = f.z;
	m->vs[3][0] = -float3dot(s, eye);
	m->vs[3][1] = -float3dot(u, eye);
	m->vs[3][2] = -float3dot(f, eye);
	m->vs[3][3] = 1.0f;
}

static inline void float4x4mul(float4x4 *res, const float4x4 *m1, const float4x4 *m2) {
	for (size_t i = 0; i < 4; ++i) {
		for (size_t j = 0; j < 4; ++j) {
			res->vs[j][i] = 0;
			for (size_t k = 0; k < 4; ++k)
				res->vs[j][i] += m1->vs[k][i] * m2->vs[j][k];
		}
	}
}


static inline float lerpf(float a, float b, float t) {
	return a * (1 - t) + b * t;
}

static inline float3 float3lerp(float3 a, float3 b, float t) {
	return float3add(float3mul(a, 1 - t), float3mul(b, t));
}


#endif // PSHINE_IMPL_MATH_H_
