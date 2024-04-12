#ifndef GF_LEXER_H_
#define GF_LEXER_H_
#include <uchar.h>
#include "internal.h"
#include "arena.h"

enum gf_token_type : uint8_t {
	GF_TT_ERROR,
	GF_TT_EOF,
	GF_TT_IDEN,
	GF_TT_INT_LIT,
	GF_TT_FLOAT_LIT,
	GF_TT_NON_CHAR_COUNT_,
};

static_assert(GF_TT_NON_CHAR_COUNT_ < ' ');

struct gf_tok_ {
	union {
		size_t as_offset;
		uint64_t as_uint64;
		float as_float;
	} value;
	uint64_t offset : 56;
	uint64_t kind : 8;
};

struct gf_tok_data_ {
	uint32_t size;
	char data[];
};

struct gf_lexer_ {
	const char *input;
	size_t input_size;
	size_t off_bytes, off_chars;
	struct gf_message_store_ msgs;
	struct gf_arena_ data;

	bool did_peek;
	struct gf_tok_ tok_buf;
	char32_t nextc;
};

void gf_lex_(struct gf_lexer_ *l);

#endif // GF_LEXER_H_
