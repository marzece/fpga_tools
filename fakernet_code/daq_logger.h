#ifndef __DAQ_LOGGER_H__
#define __DAQ_LOGGER_H__
#include <stdio.h>
#include <stdarg.h>
#include "hiredis/hiredis.h"

#define LOG_NEVER 0
#define LOG_DEBUG 1
#define LOG_INFO 2
#define LOG_WARN 3
#define LOG_ERROR 4

typedef struct Logger {
    FILE* file;
    redisContext* redis;
    const char* name;
    int verbosity_stdout;
    int verbosity_redis;
    int verbosity_file;
    char* message_buffer;
    int message_max_length;
} Logger;

extern Logger* the_logger;
void setup_logger(const char* logID, const char* redis_host, const char* log_filename,
                  int verbosity_stdout, int verbosity_file, int verbosity_redis, size_t buffer_size);
void cleanup_logger(void);

void daq_log_raw(int level, const char* format, va_list args);

// The below __attribute__ thingy tells the GNU compiler to avoid throw an
// error/warning if the format and ensuing parameters don't match up.
#ifdef __GNUC__
void daq_log(int level, const char* format, ...)
    __attribute__((format(printf, 2, 3)));
#else
void daq_log(int level, const char* format, ...);
#endif

#endif
