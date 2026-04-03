#ifndef PSHINE_PERF_H_
#define PSHINE_PERF_H_

#define PSHINE_ENABLE_PERF 1

#ifndef PSHINE_ENABLE_PERF
#define PSHINE_ENABLE_PERF 0
#endif

#define PSHINE_PERF_FUNC() PSHINE_PERF_ZONE(__func__)

#if PSHINE_ENABLE_PERF
#define TRACY_ENABLE
// #include "tracy"

#if !defined(__clang__) && !defined(__GNUC__) && !__has_attribute(cleanup)
#error For performance tracing, only Clang/GCC are supported (attribute cleanup)
#endif

#define PSHINE_PERF_ZONE(NAME)

#else

#define PSHINE_PERF_ZONE(NAME)
#endif


#endif
