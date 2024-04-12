#include "rst.h"
#include "parser.h"

#include <giraffe/util.h>
#include <pshine/util.h>
#include <stdlib.h>
#include <string.h>

// [[maybe_unused]]
// static void test(FILE *fin) {
// 	struct owl_tree *tree = owl_tree_create_with_options((struct owl_tree_options){
// 		.file = fin
// 	});
// 	struct owl_program program = owl_tree_get_owl_program(tree);
// 	(void)program.top;
// 	.type == OWL_STRUCT_DEF
// }

static void parse_top_(struct owl_top *top) {
	switch (top->type) {
		case OWL_FUNC_DEF: {
			struct owl_func_def d = owl_func_def_get(top->func_def);
			// owl_identifier_get(d.name).identifier
			(void)d;
		} break;
		case OWL_STRUCT_DEF: {
			struct owl_struct_def d = owl_struct_def_get(top->struct_def);
			// owl_identifier_get(d.name).identifier
			(void)d;
		} break;
		default: PSHINE_PANIC("bad top type");
	}
}

[[maybe_unused]]
static void parse_tree_(struct owl_tree *tree) {
	struct owl_program program = owl_tree_get_owl_program(tree);
	for (struct owl_ref r = program.top; !r.empty; r = owl_next(r)) {
		struct owl_top top = owl_top_get(r);
		parse_top_(&top);
	}
}

void gf_rst_copy_attrs(struct gf_rst_attrs *attrs, size_t count, const struct gf_rst_attr *attrs_ref) {
	attrs->count = count;
	if (attrs->count == 1) attrs->one_attr = attrs_ref[0];
	else {
		attrs->attrs = calloc(attrs->count, sizeof(struct gf_rst_attr));
		memcpy(attrs->attrs, attrs_ref, attrs->count * sizeof(struct gf_rst_attr));
	}
}

struct gf_rst_lval_arg *gf_create_rst_lval_arg(
	struct gf_ty *ty,
	bool mut,
	size_t attr_count,
	struct gf_rst_attr *attrs,
	const char8_t *name
) {
	struct gf_rst_lval_arg *p = calloc(1, sizeof(struct gf_rst_lval_arg));
	p->as_base.kind = GF_RST_LVAL_ARG;
	p->as_base.ty = ty;
	p->as_base.mut = mut;
	gf_rst_copy_attrs(&p->attrs, attr_count, attrs);
	p->name = gf_clone_utf8s(gf_clone_utf8s(name));
	return p;
}

struct gf_rst_lval_local *gf_create_rst_lval_local(
	struct gf_ty *ty,
	bool mut,
	size_t attr_count,
	struct gf_rst_attr *attrs,
	const char8_t *name
) {
	struct gf_rst_lval_local *p = calloc(1, sizeof(struct gf_rst_lval_local));
	p->as_base.kind = GF_RST_LVAL_LOCAL;
	p->as_base.ty = ty;
	p->as_base.mut = mut;
	gf_rst_copy_attrs(&p->attrs, attr_count, attrs);
	p->name = gf_clone_utf8s(gf_clone_utf8s(name));
	return p;
}

void gf_free_rst_lval(struct gf_rst_lval *v) {
	PSHINE_CHECK(v, "gf_free_rst_lval(NULL)");
	switch (v->kind) {
		case GF_RST_LVAL_ARG: free(((struct gf_rst_lval_arg*)v)->name); break;
		case GF_RST_LVAL_LOCAL: free(((struct gf_rst_lval_local*)v)->name); break;
		default: PSHINE_WARN("unknown lval kind"); break;
	}
	free(v);
}
