#ifndef GF_UTIL_H_
#define GF_UTIL_H_
#include <giraffe/giraffe.h>
#include <stdlib.h>
#include <string.h>

static inline size_t gf_utf8s_len(gf_utf8s s) {
	return strlen((char*)s);
}

static inline gf_mut_utf8s gf_clone_utf8s(gf_utf8s s) {
	size_t l = gf_utf8s_len(s);
	gf_mut_utf8s p = malloc(l + 1);
	memcpy(p, s, l + 1);
	return p;
}

#endif // GF_UTIL_H_
