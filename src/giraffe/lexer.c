#include "internal.h"
#include <stdint.h>
#include <uchar.h>

struct gf_lexer {
	const char *input;
	size_t input_size;
	size_t off_bytes, off_chars;
};

char32_t read_utf8(struct gf_lexer *l) {
	char32_t c32;
	mbstate_t p;
	size_t d = mbrtoc32(&c32, l->input + l->off_bytes, l->input_size - l->off_bytes, &p);
	if (d >= (size_t)(-3)) {
		
	}
	return c32;
}

void gf_lex_() {
	char32_t c;
}
