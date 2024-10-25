#include "internal.h"
#include <string.h>

void gf_report_(struct gf_message_store_ *store, const struct gf_message *msg) {
	store->msgs.ptr[PSHINE_DYNA_ALLOC(store->msgs)] = *msg;
}

const char *gf_message_id_string_(enum gf_message_id id) {
	switch (id) {
#define O(enum_name, padded_number, id, name, ...) case GF_MESSAGE_##padded_number##_##enum_name: return name;
		GF_MESSAGE_IDS_(O, );
		default: return "unknown";
	}
}


void gf_format_message_(struct gf_source_store_ *store, FILE *fout, const struct gf_message *msg) {
	struct gf_source_ *source = &store->sources.ptr[msg->source];
	fprintf(fout, "E%04u at %s:%lu:%lu: %s\n", msg->id, source->fpath, msg->span.begin, 0LU, gf_message_id_string_(msg->id));
}

static void gf_free_source_data_(struct gf_source_ *source) {
	if (source->cached_data) free(source->cached_data);
	source->cached_data = NULL;
	source->cached_data_size = 0;
}
static void gf_read_source_data_(struct gf_source_ *source) {
	if (!source->fpath) {
		source->cached_data = NULL;
		source->cached_data_size = 0;
		return;
	}
	
	gf_free_source_data_(source);

	source->cached_data = pshine_read_file(source->fpath, &source->cached_data_size);
}

void gf_init_source_(struct gf_source_ *source, const char *fpath) {
	source->fpath = NULL;
	source->cached_data = NULL;
	source->cached_data_size = 0;
	if (fpath) source->fpath = strdup(fpath);
}

void gf_deinit_source_(struct gf_source_ *source) {
	gf_free_source_data_(source);
	if (source->fpath) free(source->fpath);
}

const char *gf_get_source_data_(struct gf_source_ *source, size_t *size) {
	if (!source->cached_data) gf_read_source_data_(source);
	if (size) *size = source->cached_data_size;
	return source->cached_data;
}

