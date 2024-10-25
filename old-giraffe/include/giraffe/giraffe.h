#ifndef GIRAFFE_H_
#define GIRAFFE_H_
#include <stddef.h>
#include <stdint.h>
#include <giraffe/messages.h>
#include <uchar.h>

typedef struct gf_compiler_ *gf_compiler;
typedef struct gf_module_ *gf_module;
typedef struct gf_bundle_ *gf_bundle;

typedef const char8_t *gf_utf8s;
typedef char8_t *gf_mut_utf8s;

struct gf_compiler_info {
	void *user_pointer;
};

gf_compiler gf_create_compiler(const struct gf_compiler_info *info);
void gf_destroy_compiler(gf_compiler compiler);

enum gf_message_kind {
	GF_MESSAGE_KIND_ERROR = 1,
	GF_MESSAGE_KIND_WARNING = 2,
	GF_MESSAGE_KIND_NOTE = 3,
};

typedef uint64_t gf_source_offset;
#define GF_SOURCE_OFFSET_MAX UINT64_MAX

typedef uint64_t gf_source_ref;
#define GF_SOURCE_REF_MAX UINT64_MAX

struct gf_source_span {
	gf_source_offset begin, end;
};

#define GF_COMMA_ ,

enum gf_message_id : uint32_t {
#define O(enum_name, padded_number, id, name, ...) GF_MESSAGE_##padded_number##_##enum_name = id
	GF_MESSAGE_IDS_(O, GF_COMMA_)
#undef O
};

struct gf_message {
	enum gf_message_id id;
	enum gf_message_kind kind;
	struct gf_source_span span;
	gf_source_ref source;
};

struct gf_module_info {
	const char *source_path;
};

gf_module gf_compile_module(gf_compiler compiler, const struct gf_module_info *file);
const struct gf_message *gf_get_module_messages(gf_module module, size_t *count);
const char *gf_get_module_name(gf_module module);
void gf_destroy_module(gf_compiler compiler, gf_module module);

struct gf_source_loc {
	gf_source_ref source;
	uint32_t line, column;
};

struct gf_source_loc_span {
	gf_source_ref source;
	uint32_t begin_line, begin_column;
	uint32_t end_line, end_column;
};

struct gf_source_loc gf_compute_source_loc_from_offset(gf_compiler compiler, gf_source_ref source, gf_source_offset offset);
struct gf_source_loc_span gf_compute_source_loc_from_span(gf_compiler compiler, gf_source_ref source, struct gf_source_span span);

enum gf_bundle_kind {
	GF_BUNDLE_KIND_COMPUTE,
	GF_BUNDLE_KIND_VERTEX,
	GF_BUNDLE_KIND_FRAGMENT,
};

struct gf_bundle_info {
	uint32_t module_count;
	const gf_module *modules;
	enum gf_bundle_kind kind;
};

gf_bundle gf_link_bundle(gf_compiler compiler, const struct gf_bundle_info *file);
const struct gf_message *gf_get_bundle_messages(gf_bundle bundle, size_t *count);
void gf_destroy_bundle(gf_compiler compiler, gf_bundle bundle);

#endif // GIRAFFE_H_
