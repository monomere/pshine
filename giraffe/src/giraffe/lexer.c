#include "internal.h"
#include "lexer.h"
#include <string.h>
#include <uchar.h>

#define C32_EOF ((char32_t)-1)

static char32_t read_utf8(struct gf_lexer_ *l) {
	char32_t c32;
	mbstate_t p;
	size_t d = mbrtoc32(&c32, l->input + l->off_bytes, l->input_size - l->off_bytes, &p);
	if (d >= (size_t)(-3)) {
		gf_report_(&l->msgs, &(struct gf_message){
			.id = GF_MESSAGE_0001_INVALID_CHARACTER,
			.kind = GF_MESSAGE_KIND_ERROR,
			.span = gf_source_span_single_(l->off_chars)
		});
		return C32_EOF;
	} else {
		l->off_bytes += d;
		l->off_chars += 1;
		return c32;
	}
}

static size_t append_string(struct gf_lexer_ *l, size_t len, const char *str) {
	size_t size = sizeof(struct gf_tok_data_) + len + 1;
	size_t off = gf_arena_push_(&l->data, size);
	struct gf_tok_data_ *d = (void*)(l->data.mem + off);
	d->size = len;
	memcpy(d->data, str, len);
	d->data[len] = '\0';
	return off;
}

static inline bool is_ident_start_(char32_t c) {
	return ((c|32) >= 'a' && (c|32) <= 'z') || c == '_';
}

static inline bool is_ident_(char32_t c) {
	return is_ident_start_(c) || (c >= '0' && c <= '9');
}

struct gf_tok_ gf_read_tok_(struct gf_lexer_ *l) {
	while (l->input_size - l->off_bytes > 0) {
		// size_t begin_chars = l->off_chars;
		size_t begin_bytes = l->off_bytes;
		char32_t c = read_utf8(l);
		if (is_ident_start_(c)) {
			c = read_utf8(l);
			while (is_ident_(c)) {
				c = read_utf8(l);
			}
			size_t off = append_string(l, l->off_bytes - begin_bytes, l->input + begin_bytes);
			l->nextc = c;
			return (struct gf_tok_){
				.value.as_offset = off,
				.offset = l->off_bytes,
				.kind = GF_TT_IDEN
			};
		} else {
			l->nextc = c;
		}
	}
	return (struct gf_tok_){};
}
