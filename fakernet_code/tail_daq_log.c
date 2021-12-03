#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include "hiredis/hiredis.h"

#define MESSAGE_LOGGER_ID_BIT 0x1
#define MESSAGE_MESSAGE_BIT 0x2
#define MESSAGE_TAG_BIT 0x4
#define MESSAGE_TV_SEC_BIT 0x8
#define MESSAGE_TV_USEC_BIT 0x10
#define MESSAGE_VALID_MASK (MESSAGE_LOGGER_ID_BIT | MESSAGE_TAG_BIT | MESSAGE_MESSAGE_BIT | MESSAGE_TV_SEC_BIT)

typedef struct DAQMessage {
    const char* message_id;
    const char* logger_id;
    const char* message;
    int tag;
    struct timeval tv;
} DAQMessage;

static const char* tag_to_str[] = {"", "🐛", "ℹ️", "🤔", "⚠️"};
const int NUM_TAGS = sizeof(tag_to_str)/sizeof(tag_to_str[0]);
int loop = 1;

#ifdef __GNUC__
void logit(const char* format, ...)
    __attribute__((format(printf, 1, 2)));
#endif

void signal_handler(int signum) {
    printf("Stop signal recieved. Exiting...\n");
    static int num_kills = 0;
    if(signum == SIGINT || signum == SIGKILL) {
        num_kills +=1;
        loop = 0;
    }
    if(num_kills >= 2) {
        exit(1);
    }
}

void logit(const char* restrict format, ...) {
    va_list arglist;
    va_start(arglist, format);
        vprintf(format, arglist);
    va_end(arglist);
    fflush(stdout);
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
    const char* get_messages_command = "XREAD BLOCK 1 COUNT 50 streams daq_log %s";
    char latest_id[256];
    strcpy(latest_id, "0");
    redisContext* redis = create_redis_conn(redis_host);
    size_t i,j;
    char time_buffer[128];
    struct tm* local_time;
    redisReply* reply = NULL;

    signal(SIGINT, signal_handler);
    signal(SIGKILL, signal_handler);

    while(loop) {
        freeReplyObject(reply);
        reply = redisCommand(redis, get_messages_command, latest_id);
        if(!reply) {
            printf("tail_daq_log: Connection to redis database lost\n");
            // TODO this should try and re-establish database connection
            return 1;
        }
        if(reply->type != REDIS_REPLY_ARRAY) {
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
            int message_valid = 0;

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
                    message_valid |= 0x1;
                }
                else if(strcmp(key, "message") == 0) {
                    message.message = value;
                    message_valid |= 0x2;
                }
                else if(strcmp(key, "tag") == 0) {
                    message.tag = strtol(value, NULL, 10);
                    message_valid |= 0x4;
                }
                else if(strcmp(key, "tv_sec") == 0) {
                    message.tv.tv_sec = strtoll(value, NULL, 10);
                    message_valid |= 0x8;
                }
                else if(strcmp(key, "tv_usec") == 0) {
                    message.tv.tv_usec = strtoll(value, NULL, 10);
                    message_valid |= 0x10;
                }
            }

            if((message_valid & MESSAGE_VALID_MASK) == MESSAGE_VALID_MASK) {
                local_time = localtime(&message.tv.tv_sec);
                strftime(time_buffer, 128, "%x %X", local_time);
                const char* tag_str = (message.tag > 0 && message.tag < NUM_TAGS) ? tag_to_str[message.tag] : "???";
                logit("%s  [%s] [%s]: %s\n", tag_str, time_buffer,  message.logger_id, message.message);
            }
            strcpy(latest_id, message.message_id);
        }
    }

    redisFree(redis);
    return 0;
}
