#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <Windows.h>
#include <io.h>

#include <pshine/util.h>


void pshine_sleep_ms(size_t ms) {
	Sleep((DWORD)ms);
}

void pshine_set_cwd_to_exe() {
}

// TODO: don't panic on error, return nullptr instead.
/// Returns malloc'd buffer
char *pshine_read_file(const char *fname, size_t *size) {
	PSHINE_DEBUG("reading file '%s'", fname);
	FILE *fin = nullptr;
	errno_t err;
	if ((err = fopen_s(&fin, fname, "rb")) != 0) {
		PSHINE_PANIC("Could not read file '%s': %d", fname, err);
	};
	HANDLE hfin = (HANDLE)(uintptr_t)_get_osfhandle(_fileno(fin));
	size_t file_size = GetFileSize(hfin, nullptr);
	char *buf = calloc(file_size + 1, 1);
	PSHINE_CHECK(buf != NULL, "could not allocate memory for file");
	if (size != NULL) *size = file_size;
	fread(buf, 1, file_size, fin);
	buf[file_size] = '\0';
	return buf;
}

size_t pshine_get_mem_usage() {
// #ifndef __MINGW32__
// 	struct rusage usage;
// 	getrusage(RUSAGE_SELF, &usage);
// 	return usage.ru_maxrss;
// #else
// 	return 0;
// #endif // helix
	return 0;
}

void pshine_print_stacktrace(FILE *fout, bool color) {
	(void)fout;
	(void)color;
}

// // TODO: PSHINE_USE_CPPTRACE
// // #ifndef PSHINE_USE_CPPTRACE
// // #define PSHINE_USE_CPPTRACE
// // #endif

// #if defined(PSHINE_USE_CPPTRACE) && PSHINE_USE_CPPTRACE
// #include <ctrace/ctrace.h>

// void pshine_print_stacktrace(FILE *fout, bool color) {
// 	struct ctrace_stacktrace trace = ctrace_generate_trace(0, SIZE_MAX);
// 	ctrace_print_stacktrace(&trace, fout, color);
// 	ctrace_free_stacktrace(&trace);
// }
// #else
// // Do nothing, no library is provided.
// #endif

