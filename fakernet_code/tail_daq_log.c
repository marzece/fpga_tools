#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hiredis/hiredis.h"

typedef struct DAQMessage {
    const char* message_id;
    const char* logger_id;
    const char* message;
    int tag;
    struct timeval tv;
} DAQMessage;

static const char* tag_to_str[] = {"", "🐛", "ℹ️", "🤔", "⚠️"};

#ifdef __GNUC__
void logit(const char* format, ...)
    __attribute__((format(printf, 1, 2)));
#endif

void logit(const char* restrict format, ...) {
    va_list arglist;
    va_start(arglist, format);
        vprintf(format, arglist);
    va_end(arglist);
}

redisContext* create_redis_conn(const char* hostname) {
    logit("Opening Redis Connection\n");

    redisContext* c;
    c = redisConnect(hostname, 6379);
    if(c == NULL || c->err) {
        logit("Redis connection error %s\n", (c ? c->errstr : ""));
        redisFree(c);
        return NULL;
    }
    return c;
}

int main(int argc, char** argv) {

    const char* redis_host = "127.0.0.1";
    const char* get_messages_command = "XREAD BLOCK 0 COUNT 50 streams daq_log %s";
    char latest_id[256];
    strcpy(latest_id, "0");
    redisContext* redis = create_redis_conn(redis_host);
    int loop = 1;
    size_t i,j;
    char time_buffer[128];
    struct tm* local_time;

    redisReply* reply = NULL;
    while(loop) {
        freeReplyObject(reply);
        reply = redisCommand(redis, get_messages_command, latest_id);
        if(!reply) {
            // Not sure how to handle this
        }
        if(reply->type != REDIS_REPLY_ARRAY) {
            printf("BAD REPLY!\n");
            continue;
        }

        // From here on out no checks are done against the the reply data, if you've
        // made it here the reply SHOULD be well formatted as a stream response.
        // So the rest of the reply handling code will assume it is in fact well formatted.
        
        // 2nd level of reply is 2 element array where element 1 is the stream name ("daq_log"),
        // element two is an array. Here the 2nd level gets skipped over.
        // 3rd level of reply is N length array each element of the array
        // should be a log message
        redisReply* reply3 = reply->element[0]->element[1];

        for(i=0; i< reply3->elements; i++) {
            redisReply* message_reply = reply3->element[i];
            DAQMessage message;

            // 4th level of reply is a 2 element array first element is the stream item ID
            // 2nd element is the message contents
            message.message_id = message_reply->element[0]->str;

            // 5th (and final) level of reply is a 2N element array of key-value pairs
            // that is the actual contents of the message
            redisReply* kv_reply = message_reply->element[1];
            for(j=0; j<kv_reply->elements; j+=2) {
                const char* key = kv_reply->element[j]->str;
                const char* value = kv_reply->element[j+1]->str;
                if(strcmp(key, "logger_ID") == 0) {
                    message.logger_id = value;
                }
                else if(strcmp(key, "message") == 0) {
                    message.message = value;
                }
                else if(strcmp(key, "tag") == 0) {
                    message.tag = strtol(value, NULL, 10);
                }
                else if(strcmp(key, "tv_sec") == 0) {
                    message.tv.tv_sec = strtoll(value, NULL, 10);
                }
                else if(strcmp(key, "tv_usec") == 0) {
                    message.tv.tv_usec = strtoll(value, NULL, 10);
                }
                else {
                    // Unknown...just ignore it ?? idk
                }
            }

            local_time = localtime(&message.tv.tv_sec);
            strftime(time_buffer, 128, "%x %X", local_time);
            logit("%s  [%s] [%s]: %s\n", tag_to_str[message.tag], time_buffer,  message.logger_id, message.message);
            strcpy(latest_id, message.message_id);
        }
    }

    redisFree(redis);
    return 0;
}
