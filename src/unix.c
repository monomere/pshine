#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define PSHINE_INTERNAL_
#define PSHINE_MODNAME unix
#include <pshine/mod.h>

// returns malloc'd buffer
char *pshine_read_file(const char *fname, size_t *size) {
	// PSHINE_DEBUG("reading file '%s'", fname);
	int fdin = open(fname, O_RDONLY);
	if (fdin == -1) {
		perror("open");
		PSHINE_CHECK(fdin != -1, "could not read file '%s'", fname);
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
