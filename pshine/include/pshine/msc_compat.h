#ifdef _WIN32
#include <time.h>

typedef enum {
	CLOCK_MONOTONIC,
} clockid_t;

int clock_gettime(clockid_t type, struct timespec *tv);

#endif
