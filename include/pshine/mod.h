#if (!defined(PSMOD_H_) && defined(PSHINE_INTERNAL_)) || defined(__INTELLISENSE__)
#define PSMOD_H_
#ifndef PSHINE_MODNAME
#ifdef __INTELLISENSE__
#define PSHINE_MODNAME MOD
#else
#error PSHINE_MODNAME is not defined
#endif
#endif
#define PSHINE_STRINGIFY_0_(X) #X
#define PSHINE_STRINGIFY_(X) PSHINE_STRINGIFY_0_(X)
#define PSHINE_SMODNAME PSHINE_STRINGIFY_(PSHINE_MODNAME)

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

struct pshine_debug_file_loc {
	const char *func;
	const char *file;
	int lineno;
};

[[noreturn]]
static inline void pshine_panic_impl(
	struct pshine_debug_file_loc where,
	const char *fmt,
	...
) {
	fprintf(
		stderr,
		"\033[1;31mpshine/" PSHINE_SMODNAME " panic in %s at %s:%d\n  ",
		where.func, where.file, where.lineno
	);
	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
	fputs("\033[m\n", stderr);
	exit(1);
}

static inline void pshine_elog_impl(
	struct pshine_debug_file_loc where,
	const char *fmt,
	...
) {
	fprintf(
		stderr,
		"\033[1;31merror[pshine/" PSHINE_SMODNAME " in %s at %s:%d] ",
		where.func, where.file, where.lineno
	);
	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
	fputs("\033[m\n", stderr);
}

static inline void pshine_dlog_impl(
	struct pshine_debug_file_loc where,
	const char *fmt,
	...
) {
	fprintf(
		stderr,
		"[pshine/" PSHINE_SMODNAME " in %s at %s:%d] ",
		where.func, where.file, where.lineno
	);
	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
	fputs("\033[m\n", stderr);
}

#define PSHINE_ERROR(...) pshine_elog_impl( \
		(struct pshine_debug_file_loc){ __func__, __FILE__, __LINE__ }, \
		__VA_ARGS__\
	)

#define PSHINE_DEBUG(...) pshine_dlog_impl( \
		(struct pshine_debug_file_loc){ __func__, __FILE__, __LINE__ }, \
		__VA_ARGS__\
	)

#define PSHINE_PANIC(...) pshine_panic_impl( \
		(struct pshine_debug_file_loc){ __func__, __FILE__, __LINE__ }, \
		__VA_ARGS__\
	)

#define PSHINE_CHECK(e, m, ...) (!(e) ? pshine_panic_impl( \
		(struct pshine_debug_file_loc){ __func__, __FILE__, __LINE__ }, \
		"check failed: %s ('" #e "' evaluated to 0)", m __VA_OPT__(,) __VA_ARGS__\
	) : (void)0);

#endif // PSMOD_H_
