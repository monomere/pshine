#ifndef PSHINE_PERF_H_
#define PSHINE_PERF_H_

#ifndef PSHINE_ENABLE_PERF
#define PSHINE_ENABLE_PERF 0
#endif

#define PSHINE_PERF_FUNC() PSHINE_PERF_ZONE(__func__)

#if PSHINE_ENABLE_PERF
#ifndef TRACY_ENABLE
#define TRACY_ENABLE
#endif
#include <tracy/public/tracy/TracyC.h>

#if !defined(__clang__) && !defined(__GNUC__) && !__has_attribute(cleanup)
#error For performance tracing, only Clang/GCC are supported (attribute cleanup)
#endif

[[gnu::always_inline]]
static inline void pshine_perf_end_tracy_zone(TracyCZoneCtx *ctx) {
	TracyCZoneEnd(*ctx);
}

#define PSHINE_PERF_ZONE(NAME) \
	TracyCZoneN(_pshine_perf_tracy_zone __attribute__((cleanup(pshine_perf_end_tracy_zone))), NAME, true)

#define PSHINE_PERF_FRAME_MARK() do { TracyCFrameMark } while(0)
#else

#define PSHINE_PERF_ZONE(NAME) do {} while(0);
#define PSHINE_PERF_FRAME_MARK() do {} while(0);
#endif


#endif
