#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "hiredis/hiredis.h"
#include "daq_logger.h"

#define DEFAULT_REDIS_LOG_STREAM_ID "daq_log"

Logger* the_logger = NULL;
static const char* log_levels[5] = {"", "DEBUG", "INFO", "WARN", "ERROR"};

void setup_logger(const char* logID, const char* redis_host, const char* log_filename,
                  int verbosity_stdout, int verbosity_file, int verbosity_redis, size_t buffer_size) {
    Logger* logger = malloc(sizeof(Logger));

    logger->name = logID;
    logger->verbosity_stdout = verbosity_stdout;
    logger->message_buffer = malloc(buffer_size);
    logger->message_max_length = buffer_size;

    if(log_filename) {
        logger->file = fopen(log_filename, "a");;
        logger->verbosity_file = verbosity_file;
    } else {
        logger->file = NULL;
        logger->verbosity_file = LOG_NEVER;
    }
    if(redis_host) {
        logger->redis = redisConnect(redis_host, 6379);
        logger->verbosity_redis = verbosity_redis;
    } else {
        logger->redis = NULL;
        verbosity_redis = LOG_NEVER;
    }
    logger->add_newlines = 0;

    // The daq_logger code will use "the_logger"
    the_logger = logger;

    // If there were errors connecting/opening, handle those now.
    if(logger->file == NULL) {
        logger->verbosity_file = LOG_NEVER;
        daq_log(LOG_ERROR, "Could not open log file!\n");
    }
    if(logger->redis == NULL) {
        logger->verbosity_redis = LOG_NEVER;
        daq_log(LOG_ERROR, "Could not connect to redis for logging!\n");
    }
}

void cleanup_logger() {
    redisFree(the_logger->redis);
    the_logger->redis = NULL;
    fclose(the_logger->file);
    the_logger->file = NULL;
    free(the_logger->message_buffer);
    the_logger->message_buffer = NULL;
    free(the_logger);
    the_logger = NULL;
}

void redis_log_message(char* message) {
    // TODO, right now this can/will block,
    // I REALLY don't want it to block at all so I need to fix that
    // I probably will have to use hiredis's ASYNC context.
    // TODO it'd be cool to to have this send the verbosity level & time
    // as seperate key-value pairs instead of all bundled up in the message
    const char* name = the_logger->name;
    redisReply* reply;
    reply = redisCommand(the_logger->redis, "XADD %s MAXLEN ~ 500 * logger_ID %s message %s",
                                            DEFAULT_REDIS_LOG_STREAM_ID, name, message);
    // I could check the reply to make sure the command succeeded, but right
    // now I'll just have this fail silently
    freeReplyObject(reply);
}

void daq_log(int level, const char* restrict format, ...) {
    va_list arglist;

    if(!the_logger) {
        return;
    }
    if(level < the_logger->verbosity_file && 
       level < the_logger->verbosity_redis && 
       level < the_logger->verbosity_stdout) { return; }

    va_start(arglist, format);
        daq_log_raw(level, format, arglist);
    va_end(arglist);
}

void daq_log_raw(int level, const char* format, va_list args) {
    int offset;
    struct tm* tm_time;
    struct timeval tv_time;
    const char *tag = (level >= LOG_DEBUG && level <= LOG_ERROR) ? log_levels[level] : "???";

    if(!the_logger) {
        return;
    }
    if(level < the_logger->verbosity_file &&
       level < the_logger->verbosity_redis &&
       level < the_logger->verbosity_stdout) { return; }

    gettimeofday(&tv_time, NULL);
    tm_time = localtime(&tv_time.tv_sec);

    offset = strftime(the_logger->message_buffer, the_logger->message_max_length, "%D %T", tm_time);
    offset += snprintf(the_logger->message_buffer+offset, the_logger->message_max_length-offset, " [%s]: ", tag);

    vsnprintf(the_logger->message_buffer+offset, the_logger->message_max_length-offset, format, args);

    const char* my_format_string = the_logger->add_newlines ? "%s\n" : "%s";
    if(the_logger->file && level >= the_logger->verbosity_file) {
        fprintf(the_logger->file, my_format_string, the_logger->message_buffer);
    }
    if(level >= the_logger->verbosity_stdout) {
        printf(my_format_string, the_logger->message_buffer);
    }
    if(the_logger->redis && level >= the_logger->verbosity_redis) {
        redis_log_message(the_logger->message_buffer);
    }


}
