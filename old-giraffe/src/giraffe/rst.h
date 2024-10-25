#ifndef GF_RST_H_
#define GF_RST_H_
#include <giraffe/giraffe.h>
#include <stdint.h>
// Resolved Syntax Tree

struct gf_ty {

};

enum gf_ty_scalar_kind {
	GF_SCALAR_HALF,
	GF_SCALAR_SINGLE,
	GF_SCALAR_DOUBLE,
	GF_SCALAR_SHORT,
	GF_SCALAR_INT,
	GF_SCALAR_LONG,
	GF_SCALAR_USHORT,
	GF_SCALAR_UINT,
	GF_SCALAR_ULONG,
};

struct gf_ty_scalar {
	struct gf_ty as_base;
	enum gf_ty_scalar_kind kind;
};

struct gf_ty_vector {
	struct gf_ty as_base;
	struct gf_ty_scalar elem_ty;
	unsigned int size;
};

struct gf_ty_matrix {
	struct gf_ty as_base;
	struct gf_ty_scalar elem_ty;
	unsigned int rows, columns;
};

enum {
	GF_ARRAY_SIZE_UNBOUNDED = SIZE_MAX
};

struct gf_ty_array {
	struct gf_ty as_base;
	struct gf_ty *elem_ty;
	size_t count; // can be GF_ARRAY_SIZE_UNBOUNDED
};

enum gf_rst_lval_kind {
	GF_RST_LVAL_ARG,
	GF_RST_LVAL_LOCAL,
	GF_RST_LVAL_REF,
	GF_RST_LVAL_COUNT_,
};

struct gf_rst_lval {
	enum gf_rst_lval_kind kind;
	struct gf_ty *ty;
	bool mut;
};

enum gf_rst_attr_kind {
	GF_RST_ATTR_NULL,
	GF_RST_ATTR_ENTRY,
	GF_RST_ATTR_INPUT,
	GF_RST_ATTR_OUTPUT,
	GF_RST_ATTR_VERTEX,
	GF_RST_ATTR_BUILTIN,
	GF_RST_ATTR_COUNT_,
};

struct gf_rst_attr {
	enum gf_rst_attr_kind kind;
	union {
		gf_mut_utf8s as_entry_name;
		gf_mut_utf8s as_builtin_name;
	} params;
};

struct gf_rst_attrs {
	size_t count;
	union {
		struct gf_rst_attr *attrs;
		struct gf_rst_attr one_attr;
	};
};

void gf_rst_copy_attrs(struct gf_rst_attrs *attrs, size_t count, const struct gf_rst_attr *attrs_ref);

static inline struct gf_rst_attr *gf_rst_get_attrs_ptr(struct gf_rst_attrs *attrs) {
	return attrs->count == 1 ? &attrs->one_attr : attrs->attrs;
}

struct gf_rst_lval_arg {
	struct gf_rst_lval as_base;
	struct gf_rst_attrs attrs;
	gf_mut_utf8s name;
};

struct gf_rst_lval_local {
	struct gf_rst_lval as_base;
	struct gf_rst_attrs attrs;
	gf_mut_utf8s name;
};

struct gf_rst_lval_arg *gf_create_rst_lval_arg(
	struct gf_ty *ty,
	bool mutable,
	size_t attr_count,
	struct gf_rst_attr *attrs,
	gf_utf8s name
);
struct gf_rst_lval_local *gf_create_rst_lval_local(
	struct gf_ty *ty,
	bool mutable,
	size_t attr_count,
	struct gf_rst_attr *attrs,
	gf_utf8s name
);
void gf_free_rst_lval(struct gf_rst_lval *v);

#endif // GF_RST_H_
