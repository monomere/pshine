#include <time.h>
#include <Windows.h>

#include <pshine/msc_compat.h>

// https://stackoverflow.com/a/51974214/9110517

#define MS_PER_SEC      1000ULL     // MS = milliseconds
#define US_PER_MS       1000ULL     // US = microseconds
#define HNS_PER_US      10ULL       // HNS = hundred-nanoseconds (e.g., 1 hns = 100 ns)
#define NS_PER_US       1000ULL

#define HNS_PER_SEC     (MS_PER_SEC * US_PER_MS * HNS_PER_US)
#define NS_PER_HNS      (100ULL)    // NS = nanoseconds
#define NS_PER_SEC      (MS_PER_SEC * US_PER_MS * NS_PER_US)

static int clock_gettime_monotonic(struct timespec *tv) {
	static LARGE_INTEGER ticksPerSec;
	LARGE_INTEGER ticks;

	if (!ticksPerSec.QuadPart) {
		QueryPerformanceFrequency(&ticksPerSec);
		if (!ticksPerSec.QuadPart) {
			errno = ENOTSUP;
			return -1;
		}
	}

	QueryPerformanceCounter(&ticks);

	tv->tv_sec = (long)(ticks.QuadPart / ticksPerSec.QuadPart);
	tv->tv_nsec = (long)(((ticks.QuadPart % ticksPerSec.QuadPart) * NS_PER_SEC) / ticksPerSec.QuadPart);

	return 0;
}

int clock_gettime(clockid_t type, struct timespec *tp) {
	if (type == CLOCK_MONOTONIC)
		return clock_gettime_monotonic(tp);

	errno = ENOTSUP;
	return -1;
}
