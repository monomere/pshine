import math, dataclasses, re, itertools, sys, typing, functools

HAVE_FIXP = False

HEADER = f"""
#include <stddef.h>{'\n#include <stdint.h>' if HAVE_FIXP else ''}
#include <string.h>
#include <math.h>

#define MATH_FN_ static inline
#define MATH_FAST_FN_ MATH_FN_
""".strip()

CONSTS = f"""
static const double π = {math.pi};
static const double euler = {math.e};
static const double τ = {math.tau};
static const float float_pinfty = +0x1.fffffep+127f;
static const float float_ninfty = -0x1.fffffep+127f;
""".strip()

Q = (43,20)
Qn = f"Q{Q[0]}{Q[1]}"

FIXP_IMPL = R"""
#define QFP_FRAC `qs
typedef union { int64_t i; uint64_t u; } `T;

// https://stackoverflow.com/a/31662911/19776006
MATH_FAST_FN_ void fixp__umul64wide_(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo) {
	uint64_t a_lo = (uint64_t)(uint32_t)a;
	uint64_t a_hi = a >> 32;
	uint64_t b_lo = (uint64_t)(uint32_t)b;
	uint64_t b_hi = b >> 32;

	uint64_t p0 = a_lo * b_lo;
	uint64_t p1 = a_lo * b_hi;
	uint64_t p2 = a_hi * b_lo;
	uint64_t p3 = a_hi * b_hi;

	uint32_t cy = (uint32_t)(((p0 >> 32) + (uint32_t)p1 + (uint32_t)p2) >> 32);

	*lo = p0 + (p1 << 32) + (p2 << 32);
	*hi = p3 + (p1 >> 32) + (p2 >> 32) + cy;
}

MATH_FAST_FN_ void fixp__mul64wide_(int64_t a, int64_t b, int64_t *hi, int64_t *lo) {
	fixp__umul64wide_((uint64_t)a, (uint64_t)b, (uint64_t *)hi, (uint64_t *)lo);
	if (a < 0LL) *hi -= b;
	if (b < 0LL) *hi -= a;
}

MATH_FAST_FN_ int64_t fixp__mul_(int64_t a, int64_t b) {
	int64_t res;
	int64_t hi, lo;
	fixp__mul64wide_(a, b, &hi, &lo);
	res = ((uint64_t)hi << (64 - QFP_FRAC)) | ((uint64_t)lo >> QFP_FRAC);
	return res;
}

MATH_FAST_FN_ `T `$add(`T a, `T b) { return (`T){ a.i + b.i }; }
MATH_FAST_FN_ `T `$sub(`T a, `T b) { return (`T){ a.i - b.i }; }
MATH_FAST_FN_ `T `$mul(`T a, `T b) { return (`T){ fixp__mul_(a.i, b.i) }; }
MATH_FAST_FN_ `T `$div(`T a, `T b) { return (`T){ (a.i / b.i) << QFP_FRAC }; }
MATH_FAST_FN_ double double_`T(`T x) { return (double)x.i / (double)(1 << QFP_FRAC); }
MATH_FAST_FN_ float float_`T(`T x) { return (float)x.i / (float)(1 << QFP_FRAC); }
MATH_FAST_FN_ `T `T_double(double x) { return (`T){ (x * (1 << QFP_FRAC)) }; }
MATH_FAST_FN_ `T `T_float(float x) { return (`T){ (x * (1 << QFP_FRAC)) }; }
MATH_FAST_FN_ `T `$neg(`T x) { return (`T){ -x.i }; }
MATH_FAST_FN_ bool `$lt(`T a, `T b) { return a.i < b.i; }
MATH_FAST_FN_ bool `$gt(`T a, `T b) { return a.i > b.i; }
MATH_FAST_FN_ bool `$le(`T a, `T b) { return a.i <= b.i; }
MATH_FAST_FN_ bool `$ge(`T a, `T b) { return a.i >= b.i; }
MATH_FAST_FN_ `T `$fabs(`T x) { return (`T){ .u = x.u & ~(1UL << 63) }; }
MATH_FAST_FN_ `T `$sqrt(`T x) { return `T_double(sqrt(double_`T(x))); }
MATH_FAST_FN_ `T `$tan(`T x) { return `T_double(tan(double_`T(x))); }
MATH_FAST_FN_ `T `$cos(`T x) { return `T_double(cos(double_`T(x))); }
MATH_FAST_FN_ `T `$sin(`T x) { return `T_double(sin(double_`T(x))); }
""".strip()

TEMPLATES: list[tuple[set[str], str, str]] = [
	({"v"}, "{name} type", R"""
/// A `[$Dim,0,$At,$Str]-dimensional vector of `B.s.
typedef union {
	struct { `B `[xyzw,$Dim,0,$At,$CutEnd,$SeqC]; };
	struct { `B `[rgba,$Dim,0,$At,$CutEnd,$SeqC]; };
	`B vs[`[$Dim,0,$At,$Str]];
} `T;

/// Create a vector with all components taken from the arguments.
MATH_FN_ `T `T`[xyzw,$Dim,0,$At,$CutEnd,$SeqJ](`[xyzw,$Dim,0,$At,$CutEnd,`B {0},$Map,$SeqC]) { return `[xyzw,$Dim,0,$At,$CutEnd,$ctor]; }
/// Create a vector with all components taken from the arguments.
MATH_FN_ `T `T`[rgba,$Dim,0,$At,$CutEnd,$SeqJ](`[rgba,$Dim,0,$At,$CutEnd,`B {0},$Map,$SeqC]) { return `[rgba,$Dim,0,$At,$CutEnd,$ctor]; }
/// Create a vector with all components taken from the specified array.
MATH_FN_ `T `$vs(const `B vs[`[$Dim,0,$At,$Str]]) { return `[vs{0},$Dim,,$Dims,$ctor]; }
/// Create a vector with all components equal to the specified value.
MATH_FN_ `T `$v(`B v) { return `[v,$Dim,,$Dims,$ctor]; }
/// Create a vector with all components equal to 0.
MATH_FN_ `T `$v0() { return `$v(`[$bZero]); }
""".rstrip()),
	({"r"}, "{name} type", R"""
/// A `B rotor, representing rotation in 3D space.
typedef union {
	struct { `B s, xy, yz, zx; };
	`B vs[4];
} `T;

/// Create a rotor from a scalar, the xy bivector, the yz bivector, and the zx bivector.
MATH_FN_ `T `T`[wxyz,$Dim,0,$At,$CutEnd,$SeqJ](`[wxyz,$Dim,0,$At,$CutEnd,`B {0},$Map,$SeqC]) { return `[wxyz,$Dim,0,$At,$CutEnd,$ctor]; }
/// Create a rotor with all components taken from the arguments.
MATH_FN_ `T `$vs(const `B vs[`[$Dim,0,$At,$Str]]) { return `[vs{0},$Dim,,$Dims,$ctor]; }
/// Create a rotor with all components equal to the specified value.
MATH_FN_ `T `$v(`B v) { return `[v,$Dim,,$Dims,$ctor]; }
/// Create a rotor with all components equal to 0.
MATH_FN_ `T `$v0() { return `$v(`[$bZero]); }
	
/// Create a rotor that is a combination of two rotors.
MATH_FN_ `T `$combine(`T a, `T b) {
	`T result = {};
	result.s  = a.s * b.s  - a.xy * b.xy - a.yz * b.yz - a.zx * b.zx;
	result.xy = a.s * b.xy + a.xy * b.s  - a.yz * b.zx + a.zx * b.yz;
	result.yz = a.s * b.yz + a.xy * b.zx + a.yz * b.s  - a.zx * b.xy;
	result.zx = a.s * b.zx - a.xy * b.yz + a.yz * b.xy + a.zx * b.s ;
	return result;
}
	
/// Create a rotor that is the inverse of the specified rotor.
MATH_FN_ `T `$inverse(`T v) { return `$wxyz(v.s, -v.xy, -v.yz, -v.zx); }

/// Apply a rotor to a vector.
MATH_FN_ `B3 `$apply(`T r, `B3 v) {
	const `B S_x = r.s * v.x + r.xy * v.y - r.zx * v.z;
	const `B S_y = r.s * v.y - r.xy * v.x + r.yz * v.z;
	const `B S_z = r.s * v.z - r.yz * v.y + r.zx * v.x;
	const `B S_xyz = r.xy * v.z + r.yz * v.x + r.zx * v.y;
	return `B3xyz(
		S_x * r.s + S_y   * r.xy + S_xyz * r.yz - S_z   * r.zx,
		S_y * r.s - S_x   * r.xy + S_z   * r.yz + S_xyz * r.zx,
		S_z * r.s + S_xyz * r.xy - S_y   * r.yz + S_x   * r.zx
	);
}

/// Create a rotor from Euler angles, in Y-X-Z sequence.
/// First the aircraft does a yaw turn (Y), taxiing to the runway,
/// then it takes off, pitches (X), then rolls (Z).
MATH_FN_ `T `$euler(`B pitch, `B yaw, `B roll) {
	const `B cr = `[roll,0.5,$Tb,mul,$Gbinop,$cos];
	const `B sr = `[roll,0.5,$Tb,mul,$Gbinop,$sin];
	const `B cp = `[pitch,0.5,$Tb,mul,$Gbinop,$cos];
	const `B sp = `[pitch,0.5,$Tb,mul,$Gbinop,$sin];
	const `B cy = `[yaw,0.5,$Tb,mul,$Gbinop,$cos];
	const `B sy = `[yaw,0.5,$Tb,mul,$Gbinop,$sin];

	// TODO: verify that the sequence is correct...
	return `$wxyz(
		cr * cp * cy + sr * sp * sy,
		cr * sp * sy - sr * cp * cy,
		- cr * sp * cy - sr * cp * sy,
		sr * sp * cy - cr * cp * sy
	);
}

/// Create a rotor that transforms `[from_dir,$Code] to `[to_dir,$Code].
MATH_FN_ `T `$fromto(`B3 from_dir, `B3 to_dir) {
	from_dir = `B3norm(from_dir);
	to_dir = `B3norm(to_dir);
	const `B3 halfway = `B3norm(`B3add(from_dir, to_dir));
	return `$wxyz(
		`B3dot(from_dir, halfway),
		(halfway.x * from_dir.y) - (halfway.y * from_dir.x),
		(halfway.y * from_dir.z) - (halfway.z * from_dir.y),
		(halfway.z * from_dir.x) - (halfway.x * from_dir.z)
	);
}
""".strip()),
	({"cast"}, "", R"""
/// Create a `Ta from a `Tb with each component casted.
MATH_FN_ `Ta `Ta_`Tb(`Tb x) { return `[`[x{0},Tbb,$V,Tba,$V,$cast\],$ElWise,$ctor]; }
""".strip()),
	({"v4"}, "{name} type", R"""
/// Create a 4-vector from a 3-vector and the w component.
MATH_FN_ `T `T.xyz3w(`B3 xyz, `B w) { return `[xyz,xyz.{0},$Map,w,$SList,$Add,$ctor]; }
""".rstrip()),
	({"v"}, "{name} operations", R"""
/// Return the negative vector.
MATH_FN_ `T `$neg(`T v) { return `[`[v{0},$Tb,neg,$Gunop\],$ElWise,$ctor]; }
/// Return the sum of two vectors.
MATH_FN_ `T `$add(`T a, `T b) { return `[`[a{0},b{0},$Tb,add,$Gbinop\],$ElWise,$ctor]; }
/// Return the difference of two vectors.
MATH_FN_ `T `$sub(`T a, `T b) { return `[`[a{0},b{0},$Tb,sub,$Gbinop\],$ElWise,$ctor]; }
/// Return the product of a vector and a scalar.
MATH_FN_ `T `$mul(`T v, `B s) { return `[`[v{0},s,$Tb,mul,$Gbinop\],$ElWise,$ctor]; }
/// Return the vector divided by a scalar.
MATH_FN_ `T `$div(`T v, `B s) { return `[`[v{0},s,$Tb,div,$Gbinop\],$ElWise,$ctor]; }
/// Return the dot product of two vectors.
MATH_FN_ `B `$dot(`T a, `T b) { return `[`[a{0},b{0},$Tb,mul,$Gbinop\],$ElWise,`[{0},{1},$Tb,add,$Gbinop\],$Reduce]; }
/// Return the squared magnitude of a vector.
MATH_FN_ `B `$mag2(`T v) { return `[v,v,$dot]; }
/// Return the magnitude of a vector.
MATH_FN_ `B `$mag(`T v) { return `[v,$mag2,$sqrt]; }
/// Return the normalized vector, or the zero-vector, if the original is also a zero-vector.
MATH_FN_ `T `$norm(`T v) {
	`B m = `[v,$mag2];
	if (`[m,$fabs,$eps,$Tb,le,$Gbinop]) return (`T){};
	return `[v,m,$sqrt,$div];
}
""".rstrip()),
	({"v3"}, "{base} vector cross product", R"""
/// Return the cross product of two vectors.
MATH_FN_ `T `$cross(`T a, `T b) {
	return `T.xyz(
		`[a.y,b.z,$Tb,mul,$Gbinop,a.z,b.y,$Tb,mul,$Gbinop,$Tb,sub,$Gbinop],
		`[a.z,b.x,$Tb,mul,$Gbinop,a.x,b.z,$Tb,mul,$Gbinop,$Tb,sub,$Gbinop],
		`[a.x,b.y,$Tb,mul,$Gbinop,a.y,b.x,$Tb,mul,$Gbinop,$Tb,sub,$Gbinop]
	);
}
""".rstrip()),
	({"s", "v"}, "{name} lerp, min, max, clamp", R"""
/// Return the element-wise minimum of two values.
MATH_FN_ `T `$min(`T a, `T b) { return `[`[a{0},b{0},$Tb,lt,$Gbinop\] ? a{0} : b{0},$ElWise,$ctor]; }
/// Return the element-wise maximum of two values.
MATH_FN_ `T `$max(`T a, `T b) { return `[`[a{0},b{0},$Tb,gt,$Gbinop\] ? a{0} : b{0},$ElWise,$ctor]; }
/// Return the element-wise clamped components of a value.
MATH_FN_ `T `$clamp(`T x, `T a, `T b) { return `[x,a,$max,b,$min]; }
/// Return the linear interpolation of two values by a scalar.
MATH_FN_ `T `$lerp(`T a, `T b, `B t) { return `[a,$bOne,t,$Tb,sub,$Gbinop,$Paren,$muls,b,t,$muls,$add]; }
""".rstrip()),
	({"m"}, "{name} matrix", R"""
/// A `[$Dim,0,$At,$Str] by `[$Dim,1,$At,$Str] matrix of `B.s.
typedef union {
	struct { `B vvs[`[$Dim,0,$At,$Dim,1,$At,$Mul,$Str]]; };
	struct { `B vs[`[$Dim,0,$At,$Str]][`[$Dim,1,$At,$Str]]; };
	struct { `B`[$Dim,0,$At,$Str] v`[$Dim,0,$At,$Str]s[`[$Dim,1,$At,$Str]]; };
} `T;
""".rstrip()),
	({"m2x2"}, "{base} 2x2 square matrix operations", R"""
/// Multiply a matrix by a column vector. The matrix is treated as column-major.
MATH_FN_ `B2 `$mulv(const `T *m, `B2 v) {
	return `B2xy(
		m->vs[0][0] * v.x + m->vs[1][0] * v.y,
		m->vs[0][1] * v.x + m->vs[1][1] * v.y
	);
}
"""),
	({"m3x3"}, "{base} 3x3 square matrix operations", R"""
/// Multiply a matrix by a column vector. The matrix is treated as column-major.
MATH_FN_ `B3 `$mulv(const `T *m, `B3 v) {
	return `B3xyz(
		m->vs[0][0] * v.x + m->vs[1][0] * v.y + m->vs[2][0] * v.z,
		m->vs[0][1] * v.x + m->vs[1][1] * v.y + m->vs[2][1] * v.z,
		m->vs[0][2] * v.x + m->vs[1][2] * v.y + m->vs[2][2] * v.z
	);
}
"""),
	({"m4x4"}, "{base} 4x4 square matrix operations", R"""
/// Multiply a matrix by a column vector. The matrix is treated as column-major.
MATH_FN_ `B4 `$mulv(const `T *m, `B4 v) {
	return `B4xyzw(
		m->vs[0][0] * v.x + m->vs[1][0] * v.y + m->vs[2][0] * v.z + m->vs[3][0] * v.w,
		m->vs[0][1] * v.x + m->vs[1][1] * v.y + m->vs[2][1] * v.z + m->vs[3][1] * v.w,
		m->vs[0][2] * v.x + m->vs[1][2] * v.y + m->vs[2][2] * v.z + m->vs[3][2] * v.w,
		m->vs[0][3] * v.x + m->vs[1][3] * v.y + m->vs[2][3] * v.z + m->vs[3][3] * v.w
	);
}
"""),
	({"mS"}, "{base} square matrix operations", R"""
/// Set the specified matrix to the identity matrix.
MATH_FN_ void set`$iden(`T *m) {
	memset(m->vs, 0, sizeof(m->vs));
	`[$Dim,0,$At,m->vs[{0}\][{0}\] = 1.0,$UpTo,$SeqS];
}
/// Multiply a matrix by another matrix.
MATH_FN_ void `$mul(`T *m1, const `T *m2) {
	for (size_t i = 0; i < `[$Dim,0,$At,$Str]; ++i) {
		m1->v`[$Dim,0,$At,$Str]s[i] = `$mulv(m2, m1->v`[$Dim,0,$At,$Str]s[i]);
	}
	//for (size_t i = 0; i < `[$Dim,0,$At,$Str]; ++i) {
	//	for (size_t j = 0; j < `[$Dim,0,$At,$Str]; ++j) {
	//		res->vs[j][i] = 0;
	//		for (size_t k = 0; k < `[$Dim,0,$At,$Str]; ++k)
	//			res->vs[j][i] += m1->vs[k][i] * m2->vs[j][k];
	//	}
	//}
}
"""),
	# TODO: matrix multiplication
	({"m4x4d", "m4x4f"}, "{base} matrix operations", R"""
/// Multiply a matrix by an axis-angle transformation matrix.
MATH_FN_ void `B3x3axisangle(`B3x3 *m, `B3 axis, `B angle) {
	memset(m->vs, 0, sizeof(m->vs));
	`B a = angle, c = `[a,$cos], s = `[a,$sin];
	axis = `B3norm(axis);
	`B3 t = `B3mul(axis, 1 - c);

	`B r00 = c + t.x * axis.x;
	`B r01 = t.x * axis.y + s * axis.z;
	`B r02 = t.x * axis.z - s * axis.y;

	`B r10 = t.y * axis.x - s * axis.z;
	`B r11 = c + t.y * axis.y;
	`B r12 = t.y * axis.z + s * axis.x;

	`B r20 = t.z * axis.x + s * axis.y;
	`B r21 = t.z * axis.y - s * axis.x;
	`B r22 = c + t.z * axis.z;

	`B3x3 r;
	r.v3s[0] = `B3add(`B3add(`B3mul(m->v3s[0], r00), `B3mul(m->v3s[1], r01)), `B3mul(m->v3s[2], r02));
	r.v3s[1] = `B3add(`B3add(`B3mul(m->v3s[0], r10), `B3mul(m->v3s[1], r11)), `B3mul(m->v3s[2], r12));
	r.v3s[2] = `B3add(`B3add(`B3mul(m->v3s[0], r20), `B3mul(m->v3s[1], r21)), `B3mul(m->v3s[2], r22));
	*m = r;
}
/// Create a 3x3 Euler angles rotation matrix.
MATH_FN_ void set`B3x3rotation(`B3x3 *m, `B yaw, `B pitch, `B roll) {
	memset(m->vs, 0, sizeof(m->vs));
	`B α = pitch, β = yaw, γ = roll;
	`B sα = `[α,$sin], sβ = `[β,$sin], sγ = `[γ,$sin];
	`B cα = `[α,$cos], cβ = `[β,$cos], cγ = `[γ,$cos];

	m->vs[0][0] = cβ * cγ;
	m->vs[1][0] = cβ * sγ;
	m->vs[2][0] = -sβ;
	m->vs[0][1] = sα * sβ * cγ - cα * sγ;
	m->vs[1][1] = sα * sβ * sγ + cα * cγ;
	m->vs[2][1] = sα * cβ;
	m->vs[0][2] = cα * sβ * cγ + sα * sγ;
	m->vs[1][2] = cα * sβ * sγ - sα * cγ;
	m->vs[2][2] = cα * cβ;
}
	
/// Create a 3x3 rotation matrix from a rotor.
MATH_FN_ void set`B3x3rotationR(`B3x3 *m, `B.R r) {
	m->v3s[0] = `B.Rapply(r, `B3xyz(1, 0, 0));
	m->v3s[1] = `B.Rapply(r, `B3xyz(0, 1, 0));
	m->v3s[2] = `B.Rapply(r, `B3xyz(0, 0, 1));
}

/// Create a 3x3 translation matrix.
MATH_FN_ void `B4x4trans(`B4x4 *m, `B3 d) {
	`B r[4] = {};
	`[r{0} += m->vs[0\]{0} * d.x,4,,$Dims,$SeqS];
	`[r{0} += m->vs[1\]{0} * d.y,4,,$Dims,$SeqS];
	`[r{0} += m->vs[2\]{0} * d.z,4,,$Dims,$SeqS];
	`[m->vs[3\]{0} += r{0},4,,$Dims,$SeqS];
}

/// Create a 3x3 scale matrix.
MATH_FN_ void `B4x4scale(`B4x4 *m, `B3 s) {
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

/// Information returned by the perspective
/// projection matrix functions.
struct `B4x4persp_info {
	/// The near plane extents.
	`B2 plane;
	/// The distance of the near plane from the origin.
	`B znear;
};

/// Set the specified matrix to a right-handed reversed-z perspective projection matrix.
MATH_FN_ struct `B4x4persp_info set`B4x4persp_rhoz(`B4x4 *m, `B fov, `B aspect, `B znear, `B zfar) {
	// https://gist.github.com/pezcode/1609b61a1eedd207ec8c5acf6f94f53a
	memset(m->vs, 0, sizeof(m->vs));
	struct `B4x4persp_info info;
	`B t = `[fov * 0.5f * π / 180.0f,$tan];
	info.plane.y = t * znear;
	info.plane.x = info.plane.y * aspect;
	info.znear = znear;
	`B k = znear / (znear - zfar);
	`B g = 1.0 / t;
	m->vs[0][0] = g / aspect;
	m->vs[1][1] = -g;
	m->vs[2][2] = -k;
	m->vs[2][3] = 1.0;
	m->vs[3][2] = -znear * k;

	return info;
}

/// Set the specified matrix to a right-handed reversed-z infinite perspective projection matrix.
MATH_FN_ struct `B4x4persp_info set`B4x4persp_rhozi(`B4x4 *m, `B fov, `B aspect, `B znear) {
	// http://www.songho.ca/opengl/gl_projectionmatrix.html#perspective
	// https://computergraphics.stackexchange.com/a/12453
	// https://discourse.nphysics.org/t/reversed-z-and-infinite-zfar-in-projections/341/2
	memset(m->vs, 0, sizeof(m->vs));
	struct `B4x4persp_info info;
	`B t = `[fov * 0.5f * π / 180.0f,$tan];
	info.plane.y = t * znear;
	info.plane.x = info.plane.y * aspect;
	info.znear = znear;
	`B g = 1.0f / t;

	m->vs[0][0] = g / aspect;
	m->vs[1][1] = -g;
	m->vs[3][2] = znear;
	m->vs[2][3] = 1.0f;

	return info;
}

/// Set the specified matrix to a perspective projection matrix.
MATH_FN_ struct `B4x4persp_info set`B4x4persp(`B4x4 *m, `B fov, `B aspect, `B znear) {
	// return set`B4x4persp_rhoz(m, fov, aspect, znear, (`B)1000.0);
	return set`B4x4persp_rhozi(m, fov, aspect, znear);
}

/// Set the specified matrix to a view matrix, looking at `[center,$Code] from `[eye,$Code],
/// with `[up,$Code] being the upwards direction.
MATH_FN_ void set`B4x4lookat(`B4x4 *m, `B3 eye, `B3 center, `B3 up) {
	memset(m->vs, 0, sizeof(m->vs));
	`B3 f = `B3norm(`B3sub(center, eye));
	`B3 s = `B3norm(`B3cross(up, f));
	`B3 u = `B3cross(f, s);

	m->vs[0][0] = s.x;
	m->vs[1][0] = s.y;
	m->vs[2][0] = s.z;
	m->vs[0][1] = u.x;
	m->vs[1][1] = u.y;
	m->vs[2][1] = u.z;
	m->vs[0][2] = f.x;
	m->vs[1][2] = f.y;
	m->vs[2][2] = f.z;
	m->vs[3][0] = -`B3dot(s, eye);
	m->vs[3][1] = -`B3dot(u, eye);
	m->vs[3][2] = -`B3dot(f, eye);
	m->vs[3][3] = 1.0f;
}

""")
]

class Ty(typing.Protocol):
	dim: tuple[int, ...]
	is_builtin: bool
	op_map: dict[str, str]
	builtin_ops: dict[str, str]

	def fmt(self, s: str) -> str: ...
	def ctor(self, s: str) -> str: ...
	def cast_to(self, ty_to: 'Ty', s: str) -> str | None: ...
	def cast_from(self, ty_from: 'Ty', s: str) -> str | None: ...
	@property
	def name(self) -> str: ...
	@property
	def epsilon(self) -> str: ...
	@property
	def zero(self) -> str: ...
	@property
	def one(self) -> str: ...


@dataclasses.dataclass(frozen=True)
class BaseTy(Ty):
	dim: tuple[int, ...]
	_name: str
	is_builtin: bool
	op_map: dict[str, str]
	builtin_ops: dict[str, str]
	ctor_fmt: str = "({name}){{ {0} }}"
	cast_to_fmt: str = "({to})({0})"
	cast_from_fmt: str | None = None
	name_fmt: str = "{0}{name0}"
	epsilon_fmt: str = "??eps"
	zero_fmt: str = "0"
	one_fmt: str = "1"

	@property
	def name(self) -> str:
		return self._name

	def fmt(self, s: str):
		return self.name_fmt.format(s, name0=self._name[0], name=self._name)

	def ctor(self, s: str):
		return self.ctor_fmt.format(s, name=self.name)

	def cast_to(self, ty_to: 'Ty', s: str) -> str | None:
		return self.cast_to_fmt.format(s, name=self.name, to=ty_to.name)

	def cast_from(self, ty_from: 'Ty', s: str) -> str | None:
		return self.cast_from_fmt.format(s, name=self.name, **{"from": ty_from.name}) \
			if self.cast_from_fmt is not None else ty_from.cast_to(self, s)

	@property
	def epsilon(self) -> str:
		return self.epsilon_fmt.format(name=self.name)

	@property
	def zero(self) -> str:
		return self.zero_fmt.format(name=self.name)

	@property
	def one(self) -> str:
		return self.one_fmt.format(name=self.name)

@dataclasses.dataclass(frozen=True)
class RotorTy(Ty):
	dim: tuple[int, ...]
	base_ty: 'Ty'
	is_builtin: bool
	op_map: dict[str, str]
	builtin_ops: dict[str, str]
	ctor_fmt: str = "({name}){{ {0} }}"
	cast_to_fmt: str = "{to}_{name}({0})"
	cast_from_fmt: str = "{name}_{from}({0})"

	@property
	def epsilon(self) -> str:
		return self.base_ty.epsilon

	@property
	def name(self) -> str:
		return f"{self.base_ty.name}R"

	def fmt(self, s: str):
		if self.dim == (1,): return f"{s}{self.base_ty.name[0]}"
		return f"{self.name}{s}"

	def ctor(self, s: str):
		return self.ctor_fmt.format(s, name=self.name)

	def cast_to(self, ty_to: 'Ty', s: str) -> str | None:
		return self.cast_to_fmt.format(s, name=self.name, to=ty_to.name)

	def cast_from(self, ty_from: 'Ty', s: str) -> str | None:
		return self.cast_from_fmt.format(s, name=self.name, **{"from": ty_from.name}) \
			if self.cast_from_fmt is not None else ty_from.cast_to(self, s)

	@property
	def zero(self) -> str:
		return f"{self.name}v({self.base_ty.zero})"

	@property
	def one(self) -> str:
		return f"{self.name}v({self.base_ty.one})"

@dataclasses.dataclass(frozen=True)
class CompositeTy(Ty):
	dim: tuple[int, ...]
	base_ty: 'Ty'
	is_builtin: bool
	op_map: dict[str, str]
	builtin_ops: dict[str, str]
	ctor_fmt: str = "({name}){{ {0} }}"
	cast_to_fmt: str = "{to}_{name}({0})"
	cast_from_fmt: str = "{name}_{from}({0})"

	@property
	def epsilon(self) -> str:
		return self.base_ty.epsilon

	@property
	def name(self) -> str:
		if self.dim == (1,): return self.base_ty.name
		return f"{self.base_ty.name}" + "x".join(map(str, self.dim))

	def fmt(self, s: str):
		if self.dim == (1,): return f"{s}{self.base_ty.name[0]}"
		return f"{self.name}{s}"

	def ctor(self, s: str):
		return self.ctor_fmt.format(s, name=self.name)

	def cast_to(self, ty_to: 'Ty', s: str) -> str | None:
		return self.cast_to_fmt.format(s, name=self.name, to=ty_to.name)

	def cast_from(self, ty_from: 'Ty', s: str) -> str | None:
		return self.cast_from_fmt.format(s, name=self.name, **{"from": ty_from.name}) \
			if self.cast_from_fmt is not None else ty_from.cast_to(self, s)

	@property
	def zero(self) -> str:
		return f"{self.name}v({self.base_ty.zero})"

	@property
	def one(self) -> str:
		return f"{self.name}v({self.base_ty.one})"


PATTERN = re.compile(r"\`(\$?[a-zA-Z]+\.?|\[(|\\.|[^\]])*\])")
SPLIT_PATTERN = re.compile(r",\s*(?![^[\]]*\])")

def instantiate(ty: Ty, source: str, vars: dict[str, typing.Any]):
	def get_var(name: str) -> str:
		if name in vars: return vars[name]
		raise NameError(f"unknown var: {name}")

	def fn_gbinop(ty: Ty, op: str):
		if op in ty.op_map: op = ty.op_map[op]
		if op in ty.builtin_ops:
			return lambda lhs, rhs: f"{lhs} {ty.builtin_ops[op]} {rhs}"
		return lambda lhs, rhs: f"{ty.fmt(op)}({lhs}, {rhs})"

	def fn_gunop(ty: Ty, op: str):
		if op in ty.op_map: op = ty.op_map[op]
		if op in ty.builtin_ops:
			return lambda v: f"{ty.builtin_ops[op]} {v}"
		return lambda v: f"{ty.fmt(op)}({v})"

	def fn_dims(s: str, ds: tuple[int, ...] | str, prefix=""):
		if isinstance(ds, str):
			ds = tuple(map(int, ds.split(";")))
		if ds == (1,): return [s.format("")]
		return [s.format(prefix + "".join(f"[{v}]" for v in vs)) for vs in itertools.product(*map(range, ds))]
	
	def fn_cmath(name: str):
		ty2 = ty.base_ty if isinstance(ty, (CompositeTy, RotorTy)) else ty
		if ty2.name == "float": name = f"{name}f"
		elif ty2.name == "double": name = f"{name}"
		else: name = ty2.fmt(name)
		return lambda x: f"{name}({x})"
	
	# these are the exceptions, everything else is assumed to be a binary operator
	FN_MAP = {
		"ElWise": (1, lambda s: fn_dims(s, ty.dim, ".vs")),
		"Dims": (3, fn_dims),
		"ctor": (1, lambda s: ty.ctor(", ".join(s))),
		"CutEnd": (2, lambda l, a: l[:int(a)]),
		"CutStart": (2, lambda l, a: l[int(a):]),
		"Cut": (3, lambda l, a, b: l[int(a):int(b)]),
		"SeqS": (1, lambda s: "; ".join(s)),
		"SeqC": (1, lambda s: ", ".join(s)),
		"SeqP": (1, lambda s: " + ".join(s)),
		"SeqJ": (1, lambda s: "".join(s)),
		"SList": (1, lambda s: [s]),
		"EList": (1, lambda s: []),
		"Int": (1, lambda s: int(s)),
		"Str": (1, lambda x: str(x)),
		"Dim": (0, lambda: ty.dim),
		"At": (2, lambda l, s: l[int(s)]),
		"Map": (2, lambda l, s: [s.format(e) for e in l]),
		"Reduce": (2, lambda l, s: functools.reduce(lambda a, b: s.format(a, b), l)),
		"UpTo": (2, lambda n, s: [s.format(i) for i in range(n)]),
		"Add": (2, lambda a, b: a + b),
		"Sub": (2, lambda a, b: a - b),
		"Mul": (2, lambda a, b: a * b),
		"Div": (2, lambda a, b: a / b),
		"Tb": (0, lambda: ty.base_ty if isinstance(ty, (CompositeTy, RotorTy)) else ty),
		"Tt": (0, lambda: ty),
		"V": (1, lambda name: vars[name]),
		"MkVt": (2, lambda n, ty: CompositeTy((int(n),), ty, False, {"muls":"mul","mul":"??"}, {}, ctor_fmt="({name}){{{{ {} }}}}")),
		"TName": (1, lambda ty: ty.name),
		"TDim": (1, lambda ty: ty.dim),
		"TCtor": (2, lambda s, ty: ty.ctor(", ".join(s))),
		"Gbinop": (4, lambda a, b, ty, op: fn_gbinop(ty, op)(a, b)),
		"Gunop": (3, lambda a, ty, op: fn_gunop(ty, op)(a)),
		"gZero": (1, lambda ty: ty.zero),
		"gOne": (1, lambda ty: ty.one),
		"bZero": (0, lambda: ty.base_ty.zero if isinstance(ty, (CompositeTy, RotorTy)) else ty.zero),
		"bOne": (0, lambda: ty.base_ty.one if isinstance(ty, (CompositeTy, RotorTy)) else ty.one),
		"cast": (3, lambda s, tyfrom, tyto: tyto.cast_from(tyfrom, s)),
		"eps": (0, lambda: ty.epsilon),
		"neg": (1, fn_gunop(ty, "neg")),
		"mag2": (1, fn_gunop(ty, "mag2")),
		"mag": (1, fn_gunop(ty, "mag")),
		"norm": (1, fn_gunop(ty, "norm")),
		"gneg": (2, lambda x, ty: fn_gunop(ty, "neg")(x)),
		"gmag2": (2, lambda x, ty: fn_gunop(ty, "mag2")(x)),
		"gmag": (2, lambda x, ty: fn_gunop(ty, "mag")(x)),
		"gnorm": (2, lambda x, ty: fn_gunop(ty, "norm")(x)),
		"sqrt": (1, fn_cmath("sqrt")),
		"fabs": (1, fn_cmath("fabs")),
		"fmod": (1, fn_cmath("fmod")),
		"tan": (1, fn_cmath("tan")),
		"cos": (1, fn_cmath("cos")),
		"sin": (1, fn_cmath("sin")),
		"Paren": (1, lambda x: f"({x})"),
		"Code": (1, lambda a: f"`{a}`"),
	}

	def repl(m: re.Match) -> str:
		s = m.group(1).replace("\\]", "]")
		if s[0] == "[":
			stack = []
			vs = SPLIT_PATTERN.split(s[1:-1])
			for v in vs:
				v = v.strip()
				if len(v) > 0 and v[0] == '$':
					name = v[1:]
					if name in FN_MAP:
						narg = FN_MAP[name][0]
						fn = FN_MAP[name][1]
					else:
						narg = 2
						fn = fn_gbinop(ty, name)
					if len(stack) < narg:
						raise Exception(f"stack underflow for {name}, expected {narg} but only {len(stack)} available.")
					stack.append(fn(*reversed([stack.pop() for _ in range(narg)])))
				else:
					stack.append(instantiate(ty, v, vars))
			r = stack.pop()
			assert isinstance(r, str)
			return r
		elif s[0] == '$':
			if s[-1] == '.': s = s[:-1]
			r = ty.fmt(s[1:])
			assert isinstance(r, str)
			return r
		else:
			if s[-1] == '.': s = s[:-1]
			r = get_var(s)
			assert isinstance(r, str)
			return r

	return PATTERN.sub(repl, source)

BASE_BUILTIN_OPS = {
	"neg": "-",
	"add": "+",
	"sub": "-",
	"muls": "*",
	"mul": "*",
	"dot": "*",
	"div": "/",
	"lt": "<",
	"gt": ">",
	"le": "<=",
	"ge": ">=",
	"eq": "==",
	"ne": "!=",
}


class Generator:
	BASE_TYPES = (
		BaseTy((1,), "float", True, {}, BASE_BUILTIN_OPS, ctor_fmt="{}", epsilon_fmt="0.000001f"),
		BaseTy((1,), "double", True, {}, BASE_BUILTIN_OPS, ctor_fmt="{}", epsilon_fmt="0.000000001"),
	) + ((BaseTy((1,), "Qfp", False, { "muls": "mul" }, {},
			ctor_fmt="{}",
			cast_to_fmt="{to}_{name}({0})",
			cast_from_fmt="{name}_{from}({0})",
			epsilon_fmt="({name}){{2}}",
			name_fmt="{}{name}",
			zero_fmt="({name}){{0}}",
			one_fmt="({name}){{1}}",
	),) if HAVE_FIXP else ())
	BASE_DIMS = (2, 3, 4)

	def __init__(self, templates: list[tuple[set[str], str, str]], fout: typing.TextIO):
		self.templates = templates
		self.fout = fout

	def _tags(self, fmt: str, *fields: tuple[str, ...] | str) -> set[str]:
		fs = tuple((f, "") if isinstance(f, str) else f for f in fields)
		s: set[str] = set()
		for ts in itertools.product(*fs):
			s.add(fmt.format(*ts))
		return s

	def _generate_for(self, tags: set[str], ty: Ty):
		for ttags, comment, source in self.templates:
			if not ttags.isdisjoint(tags):
				# if comment:
				# 	print(f"\n// {comment.format(
				# 		name=ty.name,
				# 		base=ty.base_ty.name if isinstance(ty, (CompositeTy, RotorTy)) else ty.name,
				# 	)}", file=self.fout)
				print(instantiate(ty, source, {
					"T": ty.name,
					"B": ty.base_ty.name if isinstance(ty, (CompositeTy, RotorTy)) else ty.name,
				}), file=self.fout)

	def _generate_for2(self, tags: set[str], tya: Ty, tyb: Ty):
		# print("gen for two", tags)
		for ttags, comment, source in self.templates:
			if not ttags.isdisjoint(tags):
				# if comment:
				# 	print(f"\n// {comment.format(
				# 		namea=tya.name,
				# 		basea=tya.base_ty.name if isinstance(tya, (CompositeTy, RotorTy)) else tya.name,
				# 		nameb=tyb.name,
				# 		baseb=tyb.base_ty.name if isinstance(tyb, (CompositeTy, RotorTy)) else tyb.name,
				# 	)}", file=self.fout)
				print(instantiate(tya, source, {
					"Tta": tya,
					"Ttb": tya,
					"Tba": tya.base_ty if isinstance(tya, (CompositeTy, RotorTy)) else tya,
					"Tbb": tyb.base_ty if isinstance(tyb, (CompositeTy, RotorTy)) else tyb,
					"Ta": tya.name,
					"Ba": tya.base_ty.name if isinstance(tya, (CompositeTy, RotorTy)) else tya.name,
					"Tb": tyb.name,
					"Bb": tyb.base_ty.name if isinstance(tyb, (CompositeTy, RotorTy)) else tyb.name,
				}), file=self.fout)

	def _generate_scalar(self, ty: Ty):
		self._generate_for({"s"}, ty)
	
	def _generate_vector(self, base_ty: Ty):
		ctor = "({name}){{{{ {} }}}}"
		op_map = { "muls": "mul", "mul": "?" }
		for dim in self.BASE_DIMS:
			ty = CompositeTy((dim,), base_ty, False, op_map, {}, ctor)
			self._generate_for(self._tags("v{}{}", str(dim), ty.base_ty.name[0]), ty)
	
	def _generate_rotor(self, base_ty: Ty):
		ctor = "({name}){{{{ {} }}}}"
		op_map = { "muls": "mul", "mul": "?" }
		ty = RotorTy((4,), base_ty, False, op_map, {}, ctor)
		self._generate_for(self._tags("r{}{}", "4", ty.base_ty.name[0]), ty)
	
	def _generate_vector_casts(self, base_tya: Ty, base_tyb: Ty):
		ctor = "({name}){{{{ {} }}}}"
		op_map = { "muls": "mul", "mul": "?" }
		for dim in self.BASE_DIMS:
			tya = CompositeTy((dim,), base_tya, False, op_map, {}, ctor)
			tyb = CompositeTy((dim,), base_tyb, False, op_map, {}, ctor)
			self._generate_for2(self._tags("cast{}{}", ("", "v", f"v{dim}"), ("", *self._tags("{}{}", (base_tya.name[0], "_"), (base_tyb.name[0], "_")))), tya, tyb)
	
	def _generate_matrix_casts(self, base_tya: Ty, base_tyb: Ty):
		ctor = "({name}){{{{ {0} }}}}"
		for dim in itertools.product(self.BASE_DIMS, self.BASE_DIMS):
			tya = CompositeTy(dim, base_tya, False, {}, {}, ctor)
			tyb = CompositeTy(dim, base_tyb, False, {}, {}, ctor)
			self._generate_for2(self._tags("cast{}{}",
				("", "m", f"m{dim[0]}x_", f"m_x{dim[1]}", f"m{dim[0]}x{dim[1]}"),
				("", *self._tags("{}{}", (base_tya.name[0], "_"), (base_tyb.name[0], "_")))
			), tya, tyb)
	
	def _generate_matrix(self, base_ty: Ty):
		ctor = "({name}){{{{ {0} }}}}"
		for dim in itertools.product(self.BASE_DIMS, self.BASE_DIMS):
			ty = CompositeTy(dim, base_ty, False, {}, {}, ctor)
			self._generate_for(
				self._tags("m{}{}", ("", f"{dim[0]}x_", f"_x{dim[1]}", f"{dim[0]}x{dim[1]}"), ty.base_ty.name[0]) \
					| ({"mS"} if dim[0] == dim[1] else set()),
				ty
			)

	def generate(self):
		print("// DO NOT EDIT; THIS FILE WAS GENERATED BY generate_math.py", file=self.fout)
		print("#ifndef PSHINE_MATH_H_", file=self.fout)
		print("#define PSHINE_MATH_H_", file=self.fout)
		print(HEADER, file=self.fout)
		print("", file=self.fout)
		print(CONSTS, file=self.fout)
		print("", file=self.fout)
		if HAVE_FIXP:
			print(instantiate(
				self.BASE_TYPES[2], # type: ignore
				FIXP_IMPL,
				{ "T": "Qfp", "Qs": Q[1], "qs": str(Q[1]) }
			), file=self.fout)
		print("", file=self.fout)
		for ty in self.BASE_TYPES:
			self._generate_scalar(ty)
			self._generate_vector(ty)
			self._generate_rotor(ty)
			self._generate_matrix(ty)
		for tya, tyb in itertools.product(self.BASE_TYPES, self.BASE_TYPES):
			if tya == tyb: continue
			self._generate_vector_casts(tya, tyb)
			self._generate_matrix_casts(tya, tyb)
		print("\n#endif // PSHINE_MATH_H_", file=self.fout)

if __name__ == "__main__":
	if sys.argv[1] == "-":
		Generator(TEMPLATES, sys.stdout).generate()
	else:
		with open(sys.argv[1], "w") as fout:
			Generator(TEMPLATES, fout).generate()
