/**
 * include/core/log.h
 * Defines logging levels and the logging interface.
 */

 #ifndef ZEUS_LOG_H
 #define ZEUS_LOG_H

 #include <stdio.h>
 #include <time.h>
 #include <errno.h>

 typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
 } log_level_t;

 /**
  * Logs a message with printf-like arguments.
  */

void zeus_log(log_level_t level, const char *file, int line, const char *fmt, ...);

/**
 * Macro for error logging that includes the system error.
 */

 #define ZLOG_PERROR(fmt, ...) zeus_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt " (System Error: %s)", ##__VA_ARGS__, strerror(errno))

/**
 * Macros for standard logging, automatically capturing file and line.
 */

#define ZLOG_DEBUG(fmt, ...)  zeus_log(LOG_LEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define ZLOG_INFO(fmt, ...)  zeus_log(LOG_LEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define ZLOG_ERROR(fmt, ...) zeus_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define ZLOG_FATAL(fmt, ...) zeus_log(LOG_LEVEL_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define ZLOG_WARN(fmt, ...) zeus_log(LOG_LEVEL_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)


#endif // ZEUS_LOG_H