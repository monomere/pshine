#include <pshine/util.h>
#include <stdarg.h>

size_t pshine_log_sink_count;
struct pshine_log_sink *pshine_log_sinks;

[[noreturn]]
void pshine_panic_impl(
	struct pshine_debug_file_loc where,
	const char *fmt,
	...
) {
	for (size_t i = 0; i < pshine_log_sink_count; ++i) {
		struct pshine_log_sink *sink = &pshine_log_sinks[i];
		fprintf(
			sink->fout,
			"%spshine panic in %s at %s:%d\n  ",
			(sink->enable_color ? "\033[1;31m" : ""),
			where.func, where.file, where.lineno
		);
		va_list va;
		va_start(va, fmt);
		vfprintf(sink->fout, fmt, va);
		va_end(va);
		if (sink->enable_color)
			fputs("\033[m\n", sink->fout);
		else
			fputc('\n', sink->fout);
	}
	exit(1);
}

void pshine_log_impl(
	struct pshine_debug_file_loc where,
	enum pshine_log_severity severity,
	const char *fmt,
	...
) {
	for (size_t i = 0; i < pshine_log_sink_count; ++i) {
		struct pshine_log_sink *sink = &pshine_log_sinks[i];
		fprintf(
			sink->fout,
			"%s%s[pshine in %s at %s:%d] ",
			(sink->enable_color ? pshine_log_severity_color(severity) : ""),
			pshine_log_severity_prefix(severity),
			where.func, where.file, where.lineno
		);
		va_list va;
		va_start(va, fmt);
		vfprintf(sink->fout, fmt, va);
		va_end(va);
		if (sink->enable_color)
			fputs("\033[m\n", sink->fout);
		else
			fputc('\n', sink->fout);
	}
}
