#include "log.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    log_level_t level;
    assert(log_level_parse("warning", &level) == 0 && level == LOG_WARN);
    assert(log_level_parse("DEBUG", &level) == 0 && level == LOG_DEBUG);
    assert(log_level_parse("nope", &level) < 0);
    assert(log_level_name(LOG_ERROR)[0] == 'E');
    log_set_level(LOG_DEBUG);
    assert(log_get_level() == LOG_DEBUG);
    puts("log tests passed");
    return 0;
}
