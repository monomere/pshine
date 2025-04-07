#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#ifndef __MINGW32__
#include <sys/resource.h>
#endif
#include <fcntl.h>
#include <unistd.h>

#include <pshine/util.h>

// TODO: don't panic on error, return nullptr instead.

/// Returns malloc'd buffer
char *pshine_read_file(const char *fname, size_t *size) {
	PSHINE_DEBUG("reading file '%s'", fname);
	int fdin = open(fname, O_RDONLY);
	if (fdin == -1) {
		PSHINE_PANIC("Could not read file '%s': %s", fname, strerror(errno));
	}
	struct stat st = {};
	if (fstat(fdin, &st) == -1) {
		perror("fstat");
		PSHINE_PANIC("fstat failed");
	}
	// PSHINE_DEBUG("  file size %lu", st.st_size);
	char *buf = calloc(st.st_size + 1, 1);
	PSHINE_CHECK(buf != NULL, "could not allocate memory for file");
	if (size != NULL) *size = st.st_size;
	read(fdin, buf, st.st_size);
	buf[st.st_size] = '\0';
	return buf;
}

size_t pshine_get_mem_usage() {
#ifndef __MINGW32__
	struct rusage usage;
	getrusage(RUSAGE_SELF, &usage);
	return usage.ru_maxrss;
#else
	return 0;
#endif // helix
}

// TODO: PSHINE_USE_CPPTRACE
#ifndef PSHINE_USE_CPPTRACE
#define PSHINE_USE_CPPTRACE
#endif

#if defined(PSHINE_USE_CPPTRACE) && PSHINE_USE_CPPTRACE
#include <ctrace/ctrace.h>

void pshine_print_stacktrace(FILE *fout, bool color) {
	struct ctrace_stacktrace trace = ctrace_generate_trace(0, SIZE_MAX);
	ctrace_print_stacktrace(&trace, fout, color);
	ctrace_free_stacktrace(&trace);
}
#else
// Do nothing, no library is provided.
void pshine_print_stacktrace(FILE *fout, bool color) {}
#endif

