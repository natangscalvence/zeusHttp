/**
 * log.c
 * Implements structured logging and timestamping. 
 */

#include "../../include/core/log.h"
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

static const char *level_strings[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

void zeus_log(log_level_t level, const char *file, int line, const char *fmt, ...) {
    /**
     * Obtain timestamp.
     */

    time_t timer;
    char time_buffer[26];
    struct tm* tm_info;

    time(&timer);
    tm_info = localtime(&timer);
    strftime(time_buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(stderr, "[%s] [%s] [%d] %s:%d: ", 
            time_buffer, 
            level_strings[level], 
            getpid(),   /** Include PID */
            file, 
            line);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");

    if (level == LOG_LEVEL_FATAL) {
        exit(EXIT_FAILURE);
    }
}

