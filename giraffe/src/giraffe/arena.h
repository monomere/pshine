#ifndef GF_ARENA_H_
#define GF_ARENA_H_
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

struct gf_arena_ {
	size_t size, cap;
	uint8_t *mem;
};

/** push `size` bytes to arena. returns offset to data of `size` bytes. */
static inline size_t gf_arena_push_(struct gf_arena_ *a, size_t size) {
	size_t off = a->size;
	a->size += size;
	if (a->cap == 0) {
		while (a->size >= a->cap) a->cap += 8 * 1024; // 8KiB
		a->mem = calloc(a->cap, 1);
	} else {
		while (a->size + size >= a->cap) a->cap = a->cap + a->cap / 2;
		a->mem = realloc(a->mem, a->cap);
	}
	return off;
}

/** pop `size` bytes from arena. */
static inline void gf_arena_pop_(struct gf_arena_ *a, size_t size) {
	a->size -= size;
}

/** shrinks the capacity to fit the current size of the arena.
    if the size is zero, the memory is freed. */
static inline void gf_arena_shrink_to_fit_(struct gf_arena_ *a) {
	if (a->size == 0) {
		a->cap = 0;
		free(a->mem);
		a->mem = NULL;
		return;
	}
	while (a->cap >= 1024 && a->size <= a->cap) a->cap -= 1024;
	a->cap += 1024;
	a->mem = realloc(a->mem, a->cap);
}

/** shrinks the capacity to fit the current size of the arena. */
static inline void gf_arena_free_(struct gf_arena_ *a, size_t size) {
	a->size = 0;
	gf_arena_shrink_to_fit_(a);
}

#endif // GF_ARENA_H_
