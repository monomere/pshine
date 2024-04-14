import math, dataclasses, re, itertools, sys, typing

CONSTS = f"""
static const double π = {math.pi};
static const double euler = {math.e};
static const double τ = {math.tau};
""".strip()

TEMPLATES: list[tuple[set[str], str, str]] = [
	({"v"}, "{name} type", R"""
typedef union {
	struct { `B `[xyzw,$Dim,0,$At,$CutEnd,$SeqC]; };
	struct { `B `[rgba,$Dim,0,$At,$CutEnd,$SeqC]; };
	`B vs[`[$Dim,0,$At,$Str]];
} `T;
	
static inline `T `T`[xyzw,$Dim,0,$At,$CutEnd,$SeqJ](`[xyzw,$Dim,0,$At,$CutEnd,`B {0},$Map,$SeqC]) { return `[xyzw,$Dim,0,$At,$CutEnd,$ctor]; }
static inline `T `T`[rgba,$Dim,0,$At,$CutEnd,$SeqJ](`[rgba,$Dim,0,$At,$CutEnd,`B {0},$Map,$SeqC]) { return `[rgba,$Dim,0,$At,$CutEnd,$ctor]; }
static inline `T `$vs(const `B vs[`[$Dim,0,$At,$Str]]) { return `[vs{0},$Dim,,$Dims,$ctor]; }
static inline `T `$v(`B v) { return `[v,$Dim,,$Dims,$ctor]; }
static inline `T `$v0() { return `$v(0); }
""".strip()),
	({"castv"}, "{nameb} to {namea}", R"""
static inline `Ta `Ta_`Tb(`Tb v) { return `[(`Ba)v{0},$ElWise,$ctor]; }
""".strip()),
	({"v4"}, "{name} type", R"""
static inline `T `T.xyz3w(`B3 xyz, `B w) { return `[xyz,xyz.{0},$Map,w,$SList,$Add,$ctor]; }
""".strip()),
	({"v"}, "{name} operations", R"""
static inline `T `$neg(`T v) { return `[-v{0},$ElWise,$ctor]; }
static inline `T `$add(`T a, `T b) { return `[a{0} + b{0},$ElWise,$ctor]; }
static inline `T `$sub(`T a, `T b) { return `[a{0} - b{0},$ElWise,$ctor]; }
static inline `T `$mul(`T v, `B s) { return `[v{0} * s,$ElWise,$ctor]; }
static inline `T `$div(`T v, `B s) { return `[v{0} / s,$ElWise,$ctor]; }
static inline `B `$dot(`T a, `T b) { return `[a{0} * b{0},$ElWise,$SeqP]; }
static inline `B `$mag2(`T v) { return `[v,v,$dot]; }
static inline `B `$mag(`T v) { return `[v,$mag2,$sqrt]; }
static inline `T `$norm(`T v) {
	`B m = `[v,$mag2];
	if (`[m,$fabs] <= `eps) return (`T){};
	return `[v,m,$sqrt,$div];
}
""".strip()),
	({"v3"}, "{base} vector cross product", R"""
static inline `T `$cross(`T a, `T b) {
	return `T.xyz(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
""".strip()),
	({"s", "v"}, "{name} lerp, min, max, clamp", R"""
static inline `T `$min(`T a, `T b) { return `[a{0} < b{0} ? a{0} : b{0},$ElWise,$ctor]; }
static inline `T `$max(`T a, `T b) { return `[a{0} > b{0} ? a{0} : b{0},$ElWise,$ctor]; }
static inline `T `$clamp(`T x, `T a, `T b) { return `[x,a,$max,b,$min]; }
static inline `T `$lerp(`T a, `T b, `B t) { return `[a,1 - t,$muls,b,t,$muls,$add]; }
""".strip()),
	({"m"}, "{name} matrix", R"""
typedef union {
	struct { `B vs[`[$Dim,0,$At,$Str]][`[$Dim,1,$At,$Str]]; };
	struct { `B`[$Dim,0,$At,$Str] v`[$Dim,0,$At,$Str]s[`[$Dim,1,$At,$Str]]; };
} `T;
""".strip()),
	# TODO: matrix multiplication
	({"m4x4"}, "{base} matrix operations", R"""
static inline void set`B4x4iden(`B4x4 *m) {
	memset(m->vs, 0, sizeof(m->vs));
	m->vs[0][0] = 1.0;
	m->vs[1][1] = 1.0;
	m->vs[2][2] = 1.0;
	m->vs[3][3] = 1.0;
}

static inline void `B4x4trans(`B4x4 *m, `B3 d) {
	`B r[4] = {};
	`[r{0} += m->vs[0\]{0} * d.x,4,,$Dims,$SeqS];
	`[r{0} += m->vs[1\]{0} * d.y,4,,$Dims,$SeqS];
	`[r{0} += m->vs[2\]{0} * d.z,4,,$Dims,$SeqS];
	`[m->vs[3\]{0} += r{0},4,,$Dims,$SeqS];
}

static inline void `B4x4scale(`B4x4 *m, `B3 s) {
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

struct `B4x4persp_info {
	`B2 plane;
	`B znear;
};

static inline struct `B4x4persp_info set`B4x4persp_rhoz(`B4x4 *m, `B fov, `B aspect, `B znear, `B zfar) {
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

static inline struct `B4x4persp_info set`B4x4persp_rhozi(`B4x4 *m, `B fov, `B aspect, `B znear) {
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

static inline struct `B4x4persp_info set`B4x4persp(`B4x4 *m, `B fov, `B aspect, `B znear) {
	// return set`B4x4persp_rhoz(m, fov, aspect, znear, (`B)1000.0);
	return set`B4x4persp_rhozi(m, fov, aspect, znear);
}

static inline void set`B4x4lookat(`B4x4 *m, `B3 eye, `B3 center, `B3 up) {
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

static inline void `B4x4mul(`B4x4 *res, const `B4x4 *m1, const `B4x4 *m2) {
	for (size_t i = 0; i < 4; ++i) {
		for (size_t j = 0; j < 4; ++j) {
			res->vs[j][i] = 0;
			for (size_t k = 0; k < 4; ++k)
				res->vs[j][i] += m1->vs[k][i] * m2->vs[j][k];
		}
	}
}

""")
]

@dataclasses.dataclass(frozen=True)
class Ty:
	dim: tuple[int, ...]
	base_ty: str
	is_builtin: bool
	op_map: dict[str, str]
	builtin_ops: dict[str, str]
	cons_fmt: str
	epsilon: str

	@property
	def name(self):
		if self.dim == (1,): return self.base_ty
		return f"{self.base_ty}" + "x".join(map(str, self.dim))

	def fmt(self, s: str):
		if self.dim == (1,): return f"{s}{self.base_ty[0]}"
		return f"{self.name}{s}"

	def ctor(self, s: str):
		return self.cons_fmt.format(s, name=self.name)


PATTERN = re.compile(r"\`(\$?[a-zA-Z]+\.?|\[(|\\.|[^\]])*\])")

def instantiate(ty: Ty, source: str, vars: dict[str, str]):
	def get_var(name: str) -> str:
		if name in vars: return vars[name]
		raise NameError(f"unknown var: {name}")

	def fn_binop(op: str):
		if op in ty.op_map: op = ty.op_map[op]
		if op in ty.builtin_ops:
			return lambda lhs, rhs: f"{lhs} {ty.builtin_ops[op]} {rhs}"
		return lambda lhs, rhs: f"{ty.fmt(op)}({lhs}, {rhs})"

	def fn_unop(op: str):
		if op in ty.op_map: op = ty.op_map[op]
		if op in ty.builtin_ops:
			return lambda v: f"{ty.builtin_ops[op]} {v}"
		return lambda v: f"{ty.fmt(op)}({v})"

	def fn_dims(s: str, ds: tuple[int, ...] | str, prefix=""):
		if isinstance(ds, str):
			ds = tuple(map(int, ds.split(",")))
		if ds == (1,): return [s.format("")]
		return [s.format(prefix + "".join(f"[{v}]" for v in vs)) for vs in itertools.product(*map(range, ds))]
	
	def fn_cmath(name: str):
		if ty.base_ty == "float": name = f"{name}f"
		if ty.base_ty == "double": name = f"{name}"
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
		"Add": (2, lambda a, b: a + b),
		"neg": (1, fn_unop("neg")),
		"mag2": (1, fn_unop("mag2")),
		"mag": (1, fn_unop("mag")),
		"norm": (1, fn_unop("norm")),
		"sqrt": (1, fn_cmath("sqrt")),
		"fabs": (1, fn_cmath("fabs")),
		"fmod": (1, fn_cmath("fmod")),
		"tan": (1, fn_cmath("tan")),
	}

	def repl(m: re.Match) -> str:
		s = m.group(1).replace("\\]", "]")
		if s[0] == "[":
			stack = []
			vs = s[1:-1].split(',')
			for v in vs:
				v = v.strip()
				if len(v) > 0 and v[0] == '$':
					name = v[1:]
					if name in FN_MAP:
						narg = FN_MAP[name][0]
						fn = FN_MAP[name][1]
					else:
						narg = 2
						fn = fn_binop(name)
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
	BASE_TYPES = (("float", "0.0000001f"), ("double", "0.00000001"))
	BASE_DIMS = (2, 3, 4)

	def __init__(self, templates: list[tuple[set[str], str, str]], fout: typing.TextIO):
		self.templates = templates
		self.fout = fout

	def _generate_for(self, tags: set[str], ty: Ty):
		for ttags, comment, source in self.templates:
			if not ttags.isdisjoint(tags):
				print(f"\n// {comment.format(name=ty.name, base=ty.base_ty)}", file=self.fout)
				print(instantiate(ty, source, {
					"T": ty.name,
					"B": ty.base_ty,
					"eps": ty.epsilon
				}), file=self.fout)

	def _generate_for2(self, tags: set[str], tya: Ty, tyb: Ty):
		for ttags, comment, source in self.templates:
			if not ttags.isdisjoint(tags):
				print(f"\n// {comment.format(namea=tya.name, basea=tya.base_ty, nameb=tyb.name, baseb=tyb.base_ty)}", file=self.fout)
				print(instantiate(tya, source, {
					"Ta": tya.name,
					"Ba": tya.base_ty,
					"epsa": tya.epsilon,
					"Tb": tyb.name,
					"Bb": tyb.base_ty,
					"epsb": tyb.epsilon
				}), file=self.fout)

	def _generate_scalar(self, ty: Ty):
		self._generate_for({"s"}, ty)
	
	def _generate_vector(self, base_ty: Ty):
		cons = "({name}){{{{ {} }}}}"
		op_map = { "muls": "mul", "mul": "?" }
		for dim in self.BASE_DIMS:
			ty = Ty((dim,), base_ty.base_ty, False, op_map, {}, cons, base_ty.epsilon)
			self._generate_for({"v", f"v{dim}"}, ty)
	
	def _generate_vector_casts(self, base_tya: Ty, base_tyb: Ty):
		cons = "({name}){{{{ {} }}}}"
		op_map = { "muls": "mul", "mul": "?" }
		for dim in self.BASE_DIMS:
			tya = Ty((dim,), base_tya.base_ty, False, op_map, {}, cons, base_tya.epsilon)
			tyb = Ty((dim,), base_tyb.base_ty, False, op_map, {}, cons, base_tyb.epsilon)
			self._generate_for2({"cast", "castv"}, tya, tyb)
	
	def _generate_matrix(self, base_ty: Ty):
		cons = "({name}){{{{ {} }}}}"
		for dim in itertools.product(self.BASE_DIMS, self.BASE_DIMS):
			ty = Ty(dim, base_ty.base_ty, False, {}, {}, cons, base_ty.epsilon)
			self._generate_for({"m", f"m{dim[0]}x{dim[1]}"}, ty)

	def generate(self):
		print("// DO NOT EDIT; THIS FILE WAS GENERATED BY generate_math.py", file=self.fout)
		print("#ifndef PSHINE_MATH_H_", file=self.fout)
		print("#define PSHINE_MATH_H_", file=self.fout)
		print("#include \"pshine/util.h\"", file=self.fout)
		print("#include <stddef.h>", file=self.fout)
		print("#include <string.h>", file=self.fout)
		print("#include <math.h>", file=self.fout)
		print("", file=self.fout)
		print(CONSTS, file=self.fout)
		print("", file=self.fout)
		for base_ty, epsilon in self.BASE_TYPES:
			ty = Ty((1,), base_ty, True, {}, BASE_BUILTIN_OPS, "{}", epsilon)
			self._generate_scalar(ty)
			self._generate_vector(ty)
			self._generate_matrix(ty)
		for a, b in itertools.product(self.BASE_TYPES, self.BASE_TYPES):
			if a == b: continue
			tya = Ty((1,), a[0], True, {}, BASE_BUILTIN_OPS, "{}", a[1])
			tyb = Ty((1,), b[0], True, {}, BASE_BUILTIN_OPS, "{}", b[1])
			self._generate_vector_casts(tya, tyb)
		print("\n#endif // PSHINE_MATH_H_", file=self.fout)

if __name__ == "__main__":
	with open(sys.argv[1], "w") as fout:
		Generator(TEMPLATES, fout).generate()
