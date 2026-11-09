#ifndef OTN_LOG_H
#define OTN_LOG_H

typedef enum {
    LOG_ERROR = 0,
    LOG_WARN = 1,
    LOG_INFO = 2,
    LOG_DEBUG = 3
} log_level_t;

void log_set_level(log_level_t level);
log_level_t log_get_level(void);
void log_write(log_level_t level, const char *component, const char *format, ...);
const char *log_level_name(log_level_t level);
int log_level_parse(const char *text, log_level_t *level);

#define LOGE(component, ...) log_write(LOG_ERROR, component, __VA_ARGS__)
#define LOGW(component, ...) log_write(LOG_WARN, component, __VA_ARGS__)
#define LOGI(component, ...) log_write(LOG_INFO, component, __VA_ARGS__)
#define LOGD(component, ...) log_write(LOG_DEBUG, component, __VA_ARGS__)

#endif
