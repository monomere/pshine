#ifndef GIRAFFE_INTERNAL_H_
#define GIRAFFE_INTERNAL_H_
#include <giraffe/giraffe.h>
#include <pshine/util.h>
#include <stdio.h>

struct gf_source_ {
	char *fpath;
	size_t cached_data_size;
	char *cached_data;
};

void gf_init_source_(struct gf_source_ *source, const char *fpath);
void gf_deinit_source_(struct gf_source_ *source);
const char *gf_get_source_data_(struct gf_source_ *source, size_t *size);

struct gf_source_store_ {
	PSHINE_DYNA_(struct gf_source_) sources;
};

struct gf_message_store_ {
	PSHINE_DYNA_(struct gf_message) msgs;
};

struct gf_module_ {
	struct gf_message_store_ msgs;
};

struct gf_compiler_ {
	PSHINE_DYNA_(struct gf_module_) mods;
};

void gf_report_(struct gf_message_store_ *store, const struct gf_message *msg);
void gf_format_message_(struct gf_source_store_ *store, FILE *fout, const struct gf_message *msg);

static inline struct gf_source_span gf_source_span_unknown_() {
	return (struct gf_source_span){ .begin = GF_SOURCE_OFFSET_MAX, .end = GF_SOURCE_OFFSET_MAX };
}

static inline struct gf_source_span gf_source_span_single_(gf_source_offset offset) {
	return (struct gf_source_span){ .begin = offset, .end = offset };
}

#endif // GIRAFFE_INTERNAL_H_
