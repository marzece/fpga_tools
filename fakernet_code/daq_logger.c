#include <stdio.h>
#include <stdarg.h>
#include "hiredis/hiredis.h"
#include "daq_logger.h"

#define DEFAULT_REDIS_LOG_STREAM_ID "daq_log"

Logger* the_logger = NULL;
static const char* log_levels[5] = {"", "DEBUG", "INFO", "WARN", "ERROR"};

void redis_log_message(char* message) {
    // TODO, right now this can/will block,
    // I REALLY don't want it to block at all so I need to fix that
    // I probably will have to use hiredis's ASYNC context.
    // TODO it'd be cool to to have this send the verbosity level & time
    // as seperate key-value pairs instead of all bundled up in the message
    const char* name = "fakernet_data_builder";
    redisReply* reply;
    reply = redisCommand(the_logger->redis, "XADD %s MAXLEN ~ 500 * logger_ID %s message %s",
                                            DEFAULT_REDIS_LOG_STREAM_ID, name, message);
    // I could check the reply to make sure the command succeeded, but right
    // now I'll just have this fail silently
    freeReplyObject(reply);
}

void daq_log(int level, const char* restrict format, ...) {
    if(!the_logger) {
        return;
    }
    if(level < the_logger->verbosity_file && 
       level < the_logger->verbosity_redis && 
       level < the_logger->verbosity_stdout) { return; }

    va_list arglist;
    int offset;
    struct tm* tm_time;
    struct timeval tv_time;
    const char *tag = (level >= LOG_DEBUG && level <= LOG_ERROR) ? log_levels[level] : "???";

    gettimeofday(&tv_time, NULL);
    tm_time = localtime(&tv_time.tv_sec);

    offset = strftime(the_logger->message_buffer, the_logger->message_max_length, "%D %T", tm_time);
    offset += snprintf(the_logger->message_buffer+offset, the_logger->message_max_length-offset, " [%s]: ", tag);

    va_start(arglist, format);
        vsnprintf(the_logger->message_buffer+offset, the_logger->message_max_length-offset, format, arglist);
    va_end(arglist);

    if(the_logger->file && level >= the_logger->verbosity_file) {
        fprintf(the_logger->file, "%s", the_logger->message_buffer);
    }
    if(the_logger->verbosity_stdout >= level) {
        printf("%s", the_logger->message_buffer);
    }
    if(the_logger->redis && level >= the_logger->verbosity_redis) {
        redis_log_message(the_logger->message_buffer);
    }
}
