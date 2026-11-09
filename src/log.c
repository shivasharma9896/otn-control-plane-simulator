#define _POSIX_C_SOURCE 200809L
#include "log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static log_level_t current_level = LOG_INFO;

void log_set_level(log_level_t level) { current_level = level; }
log_level_t log_get_level(void) { return current_level; }

const char *log_level_name(log_level_t level) {
    switch (level) {
    case LOG_ERROR: return "ERROR";
    case LOG_WARN: return "WARN";
    case LOG_INFO: return "INFO";
    case LOG_DEBUG: return "DEBUG";
    default: return "UNKNOWN";
    }
}

int log_level_parse(const char *text, log_level_t *level) {
    if (!text || !level) return -1;
    if (!strcasecmp(text, "error")) *level = LOG_ERROR;
    else if (!strcasecmp(text, "warn") || !strcasecmp(text, "warning")) *level = LOG_WARN;
    else if (!strcasecmp(text, "info")) *level = LOG_INFO;
    else if (!strcasecmp(text, "debug")) *level = LOG_DEBUG;
    else return -1;
    return 0;
}

void log_write(log_level_t level, const char *component, const char *format, ...) {
    if (level > current_level) return;
    struct timespec ts;
    struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    fprintf(stderr, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ %-5s [%s] ",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
            tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000L, log_level_name(level),
            component ? component : "app");
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fputc('\n', stderr);
}
