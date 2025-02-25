#ifndef PSHINE_UTIL_H_
#define PSHINE_UTIL_H_
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

// These are defined in `main.c`
extern int pshine_argc;
extern const char **pshine_argv;

static inline char *pshine_strdup(const char *s) {
	size_t l = strlen(s) + 1;
	char *z = malloc(l);
	memcpy(z, s, l);
	return z;
}

static inline bool pshine_check_has_option(const char *opt) {
	for (int i = 1; i < pshine_argc; ++i)
		if (strcmp(pshine_argv[i], opt) == 0)
			return true;
	return false;
}

struct pshine_timeval { int64_t sec; int64_t nsec; };

static inline struct pshine_timeval pshine_timeval_delta(struct pshine_timeval t1, struct pshine_timeval t2) {
	enum { NS_PER_SECOND = 1000000000 };
	struct pshine_timeval td;
	td.nsec = t2.nsec - t1.nsec;
	td.sec  = t2.sec - t1.sec;
	if (td.sec > 0 && td.nsec < 0) {
		td.nsec += NS_PER_SECOND;
		td.sec--;
	} else if (td.sec < 0 && td.nsec > 0) {
		td.nsec -= NS_PER_SECOND;
		td.sec++;
	}
	return td;
}

#define PSHINE_TIMEVAL_FMTSTR "%ld.%.9lds"
#define PSHINE_TIMEVAL_FMT(VAL) ((VAL).sec), ((VAL).nsec)

static inline struct pshine_timeval pshine_timeval_now() {
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return (struct pshine_timeval){ .sec = tp.tv_sec, .nsec = tp.tv_nsec };
}

struct pshine_log_sink {
	FILE *fout;
	bool enable_color;
};

extern size_t pshine_log_sink_count;
extern struct pshine_log_sink *pshine_log_sinks;

struct pshine_debug_file_loc {
	const char *func;
	const char *file;
	int lineno;
};

enum pshine_log_severity {
	PSHINE_LOG_SEVERITY_CRITICAL,
	PSHINE_LOG_SEVERITY_ERROR,
	PSHINE_LOG_SEVERITY_WARNING,
	PSHINE_LOG_SEVERITY_INFO,
	PSHINE_LOG_SEVERITY_DEBUG,
};

#define PSHINE_DEBUG_FILE_LOC_HERE (struct pshine_debug_file_loc){ __func__, __FILE__, __LINE__ }

[[noreturn]]
void pshine_panic_impl(
	struct pshine_debug_file_loc where,
	const char *fmt,
	...
);

static inline const char *pshine_log_severity_color(enum pshine_log_severity severity) {
	switch (severity) {
	case PSHINE_LOG_SEVERITY_CRITICAL: return "\033[1;31m";
	case PSHINE_LOG_SEVERITY_ERROR: return "\033[0;31m";
	case PSHINE_LOG_SEVERITY_WARNING: return "\033[1;33m";
	case PSHINE_LOG_SEVERITY_INFO: return "\033[34m";
	case PSHINE_LOG_SEVERITY_DEBUG: return "";
	default: return "\033[1m";
	}
}

static inline const char *pshine_log_severity_prefix(enum pshine_log_severity severity) {
	switch (severity) {
	case PSHINE_LOG_SEVERITY_CRITICAL: return "critical";
	case PSHINE_LOG_SEVERITY_ERROR: return "error";
	case PSHINE_LOG_SEVERITY_WARNING: return "warning";
	case PSHINE_LOG_SEVERITY_INFO: return "info";
	case PSHINE_LOG_SEVERITY_DEBUG: return "";
	default: return "unknown";
	}
}

void pshine_log_impl(
	struct pshine_debug_file_loc where,
	enum pshine_log_severity severity,
	const char *fmt,
	...
);

#define PSHINE_LOG(severity, ...) pshine_log_impl(PSHINE_DEBUG_FILE_LOC_HERE, severity, __VA_ARGS__)
#define PSHINE_ERROR(...) PSHINE_LOG(PSHINE_LOG_SEVERITY_ERROR, __VA_ARGS__)
#define PSHINE_DEBUG(...) PSHINE_LOG(PSHINE_LOG_SEVERITY_DEBUG, __VA_ARGS__)
#define PSHINE_WARN(...) PSHINE_LOG(PSHINE_LOG_SEVERITY_WARNING, __VA_ARGS__)
#define PSHINE_INFO(...) PSHINE_LOG(PSHINE_LOG_SEVERITY_INFO, __VA_ARGS__)
#define PSHINE_PANIC(...) pshine_panic_impl(PSHINE_DEBUG_FILE_LOC_HERE, __VA_ARGS__)
#define PSHINE_CHECK(e, m, ...) (!(e) ? pshine_panic_impl( \
		PSHINE_DEBUG_FILE_LOC_HERE, \
		"check failed: %s ('" #e "' evaluated to 0)", m __VA_OPT__(,) __VA_ARGS__\
	) : (void)0);

#define PSHINE_MEASURE(LABEL, EXPR) do { \
	struct pshine_timeval start = pshine_timeval_now(); \
	(EXPR); \
	struct pshine_timeval end = pshine_timeval_now(); \
	struct pshine_timeval delta = pshine_timeval_delta(start, end); \
	PSHINE_INFO(LABEL " took " PSHINE_TIMEVAL_FMTSTR, PSHINE_TIMEVAL_FMT(delta)); \
} while(0)

#define PSHINE_MEASURED(LABEL, EXPR) ({ \
		struct pshine_timeval start = pshine_timeval_now(); \
		typeof(EXPR) res = (EXPR); \
		struct pshine_timeval end = pshine_timeval_now(); \
		struct pshine_timeval delta = pshine_timeval_delta(start, end); \
		PSHINE_INFO(LABEL " took " PSHINE_TIMEVAL_FMTSTR, PSHINE_TIMEVAL_FMT(delta)); \
		res; \
	})

// NB: returns a malloc'd buffer
char *pshine_read_file(const char *fname, size_t *size);

// in KiB
size_t pshine_get_mem_usage();

struct pshine_dyna_dead_item_ {
	size_t next;
};

struct pshine_dyna_ {
	void *ptr;
	size_t count, cap;
	size_t next_free;
};

#define PSHINE_DYNA_(T) union { \
	_Static_assert(sizeof(T) >= sizeof(struct pshine_dyna_dead_item_), ""); \
	struct pshine_dyna_ dyna; \
	union { \
		union { T item; struct pshine_dyna_dead_item_ dead; } *dead_ptr; \
		T *ptr; \
	}; \
}

static inline void pshine_clear_dyna_(struct pshine_dyna_ *dyna) {
	dyna->count = 0;
	dyna->next_free = 0;
}

static inline void pshine_free_dyna_(struct pshine_dyna_ *dyna) {
	pshine_clear_dyna_(dyna);
	if (dyna->cap > 0) free(dyna->ptr);
	dyna->cap = 0;
	dyna->ptr = NULL;
}

#define PSHINE_DYNA_ALLOC(a) (pshine_dyna_alloc_(&(a).dyna, sizeof(*(a).ptr)))
static inline size_t pshine_dyna_alloc_(struct pshine_dyna_ *d, size_t item_size) {
	if (d->next_free >= d->count) {
		// next_free points outside of the array, we should increase count.

		// set the next free to be outside of the array.
		d->next_free = 1 + d->count;

		size_t old_cap = d->cap; // if 0, calloc, else realloc.
	
		while (d->count >= d->cap) {
			if (d->cap == 0) {
				d->cap = 1;
			} else {
				d->cap += 256;
			}
		}

		if (old_cap == 0) {
			d->ptr = calloc(1, item_size);
		} else {
			d->ptr = realloc(
				d->ptr,
				item_size * d->cap
			);
		}

		return d->count++; // actually increase count.
	}
	// next_free points inside the array, get the dead item there
	// and set next_free to whatever it points to. like a linked list.
	// after that, the item at the old next_free is free to use.
	size_t idx = d->next_free;
	// we have to index manually because item_size is not known at compile time.
	struct pshine_dyna_dead_item_ *item
		= (void*)((uint8_t*)d->ptr + item_size * d->next_free);
	d->next_free = item->next;
	return idx;
}

#define PSHINE_DYNA_KILL(a, i) (pshine_dyna_kill_(&(a).dyna, sizeof(*(a).ptr), (i)))
static inline void pshine_dyna_kill_(struct pshine_dyna_ *d, size_t item_size, size_t idx) {
	PSHINE_CHECK(d != NULL, "empty dyna");
	PSHINE_CHECK(idx < d->count, "invalid index");
	// we save next_free into the dead element and set it to point the dead element.
	// like prepending in a linked list.
	// we have to index manually because item_size is not known at compile time.
	struct pshine_dyna_dead_item_ *item
		= (void*)((uint8_t*)d->ptr + item_size * idx);
	item->next = d->next_free;
	d->next_free = idx;
}

static inline char *pshine_format_string(const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	size_t sz = vsnprintf(NULL, 0, fmt, va);
	va_end(va);
	char *a = malloc(sz + 1);
	va_start(va, fmt);
	vsnprintf(a, sz + 1, fmt, va);
	va_end(va);
	a[sz] = '\0';
	return a;
}

static inline char *pshine_vformat_string(const char *fmt, va_list va) {
	va_list va2;
	va_copy(va2, va);
	size_t sz = vsnprintf(NULL, 0, fmt, va);
	char *a = malloc(sz + 1);
	vsnprintf(a, sz + 1, fmt, va2);
	va_end(va2);
	a[sz] = '\0';
	return a;
}

#endif // PSHINE_UTIL_H_
