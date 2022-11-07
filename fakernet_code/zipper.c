#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <inttypes.h>
#include <getopt.h>
#include <arpa/inet.h>
#include "hiredis/hiredis.h"
// The number of seperate data streams, each of which must
// be present for an event to be complete.
#define MAX_DEVICE_NUMBER 34 // Maximum device ID
#define DATA_HEADER_NBYTES 20
#define HASH_TABLE_SIZE 1000
#define DEFAULT_EVENT_MASK 0xFF1ULL

#define QUEUE_LENGTH 100
#define FONTUS_DEVICE_ID 0
#define REDIS_OUT_DATA_BUF_SIZE (32*1024*1024)
#define PUBLISH_MAX_RATE (10*1024*1024)
#define DEFAULT_FILE_SIZE_THRESHOLD (1024*1024*1024*1024ULL) // 1GB

// Minimum time between sending events to redis, in micro-seconds
// 30k us = 30ms = ~30hz
#define REDIS_COOLDOWN 30000
#define PRINT_UPDATE_COOLDOWN 1000000

// For some reason linux doesn't always have htonll...so this can be used instead
#if __linux__
#define htonll(x) ((((uint64_t)htonl(x)) << 32) + htonl((x) >> 32))
#define ntohll(x) htonll(x)
#endif

uint32_t last_seen_event[MAX_DEVICE_NUMBER];
uint64_t COMPLETE_EVENT_MASK = DEFAULT_EVENT_MASK;
int loop = 1;
int disconnect_from_redis = 0;

// TODO
typedef struct FONTUS_HEADER {
    uint32_t magic_number;
    uint32_t trig_number;
    uint64_t timestamp;
    uint16_t trig_length;
    uint8_t device_id;
    uint8_t trig_flag;
    uint32_t self_trigger_word;
    uint64_t beam_time;
    uint64_t led_time;
    uint64_t ct_time;
    uint32_t crc;
}FONTUS_HEADER;

typedef struct EVENT_HEADER {
    uint32_t trig_number;
    uint32_t status;
    uint64_t device_mask;
} EVENT_HEADER;

typedef struct FullEvent {
    int data_nbytes[MAX_DEVICE_NUMBER];
    redisReply* redis_obj[MAX_DEVICE_NUMBER];
    int counter;
} FullEvent;

typedef struct ReadyEventQueue {
    uint32_t event_ids[QUEUE_LENGTH];
    int events_available;
} ReadyEventQueue;
ReadyEventQueue event_ready_queue;

// Hash store
typedef struct EventRecord {
    uint64_t bit_word;
    //unsigned int count;
    uint32_t event_number;
    redisReply* data[MAX_DEVICE_NUMBER];
} EventRecord;
EventRecord event_registry[HASH_TABLE_SIZE];

// TODO this should be defined in some common header file
typedef struct TrigHeader {
    uint32_t magic_number;
    uint32_t trig_number;
    uint64_t clock;
    uint16_t length;
    uint8_t device_id;
    uint8_t crc;
} TrigHeader;

redisContext* create_redis_conn(const char* redis_hostname, int port) {
    printf("Opening Redis Connection\n");
    redisContext* c;
    c = redisConnect(redis_hostname, port);
    if(c == NULL || c->err) {
        printf("Redis connection error %s\n", c->errstr);
        redisFree(c);
        return NULL;
    }
    return c;
}

redisContext* create_redis_unix_conn(const char* path, int nonblock) {
    printf("Opening Redis Connection\n");
    redisContext* c;
    if(!nonblock) {
        c = redisConnectUnix(path);
    }  else {
        c = redisConnectUnixNonBlock(path);
    }
    if(c == NULL || c->err) {
        printf("Redis connection error %s\n", c->errstr);
        redisFree(c);
        return NULL;
    }
    return c;
}

void signal_handler(int signum) {
    // TODO think of more signals that would be useful
    static int num_kills = 0;
    if(signum == SIGINT || signum == SIGKILL) {
        printf("Cntrl-C caught. Will attempt graceful exit. Cntrl-C again for immediate exit.\n");
        loop = 0;
        num_kills +=1;
    }
    if(num_kills > 2) {
        printf("Dying\n");
        exit(1);
    }
}

void flag_complete_event(uint32_t event_number) {

    if(event_ready_queue.events_available == QUEUE_LENGTH) {
        // TODO figure out how this could be handled
        printf("Error?QUEUE FULL\n");
        return;
    }
    event_ready_queue.event_ids[event_ready_queue.events_available++] = event_number;
}

uint32_t pop_complete_event_id(void) {
    if(event_ready_queue.events_available == 0) {
        printf("Trying to pop empty queue. That shouldn't happen\n");
        return -1;
    }
    return event_ready_queue.event_ids[--event_ready_queue.events_available];
}

void mark_as_skipped(uint32_t device_id, uint32_t event_number) {
    (void) device_id;
    (void) event_number;
}

void register_waveform(uint32_t device_id, uint32_t event_number, redisReply* wf_data) {
    // Device number 0-3 (inclusive) are taken by the two FONTUS boards,
    // so device number 4 is the first CERES board.
    //size_t i;

    // If the waveform isn't part of the event mask just ignore it
    if(((1<<device_id) & COMPLETE_EVENT_MASK) == 0) {
        return;
    }

    int hash_value = event_number % HASH_TABLE_SIZE;
    if(event_number == last_seen_event[device_id]+1) {
        // Pass
    }
    else if(event_number > last_seen_event[device_id]+1) {
        // Events were skipped
        /*
        for(i=last_seen_event[device_id]+1; i<event_number; i++) {
            // Mark event as skipped
            // What if I skip ahead like 10 zillion events due to a readout error?
            // I dont think that should ever happen b/c of the CRC check though?
            //mark_as_skipped(device_id, i);
            printf("Events skipped FPGA-%i: %i - %i\n", device_id,
                                                        last_seen_event[device_id],
                                                        event_number);
        }
        */
    }
    else {
        // Event comes before previous event. Unclear how this happened and how
        // it should be handled...
        printf("Event skipped backwards! Why did this happen?\n");
    }
    last_seen_event[device_id] = event_number;


    if(event_registry[hash_value].bit_word == 0 || event_registry[hash_value].event_number == event_number) {
        event_registry[hash_value].event_number = event_number;
        event_registry[hash_value].bit_word |= 1ULL<<device_id;
        event_registry[hash_value].data[device_id] = wf_data;
    }
    else {
        event_registry[hash_value].event_number = event_number;
        event_registry[hash_value].bit_word = 1ULL<<device_id;
        event_registry[hash_value].data[device_id] = wf_data;
        printf("Hash collision!\n");
        //exit(1);
        // TODO, figure out how this should be handled
    }

    if(event_registry[hash_value].bit_word == COMPLETE_EVENT_MASK) {
        flag_complete_event(event_number);
        // Clear the event registry for this event
        event_registry[hash_value].bit_word = 0;
    }
}

void recieve_waveform_from_redis(redisContext* redis) {
    redisReply* reply;
    if(redisBufferRead(redis) != REDIS_OK) {
        // Error
        printf("REDIS BUF READ ERROR1: %s\n", redis->errstr);
        exit(1);

    }
    if(redisGetReplyFromReader(redis, (void**)&reply) != REDIS_OK) {
        // Error
        printf("REDIS BUF READ ERROR2: %s\n", redis->errstr);
        exit(1);
    }

    if(!reply) {
        // No data available...probably. Just move on
        return;
    }

    // I'm almost certain that a redis pub-sub message is always an array
    // The first element of the array is a string that just says "message"
    // The second element is a string that gives the channel name (don't care about that)
    // The third element is that actual data
    assert(reply->type == REDIS_REPLY_ARRAY);
    assert(reply->elements == 3);

    redisReply* rr_dat = reply->element[2];
    assert(rr_dat->type == REDIS_REPLY_STRING);
    assert(rr_dat->len >= DATA_HEADER_NBYTES); // Header should always be 20 bytes

    uint32_t event_number = ntohl(*((uint32_t*) (rr_dat->str+4)));
    uint32_t device_id = *((uint8_t*) (rr_dat->str+18));

    register_waveform(device_id, event_number, reply);
}

void grab_data_from_pubsub_message(redisReply* message, char** data, int* length) {
    if(!message) {
        *data = NULL;
        *length = 0;
        return;
    }

    redisReply* rr_dat = message->element[2];
    *data = rr_dat->str;
    *length = rr_dat->len;
}

unsigned long long save_event(FILE* fout, int event_id) {
    // If the file isn't valid I can't write to it
    if(!fout) {
        return 0;
    }

    int i;
    unsigned long long nwritten;
    EventRecord* event = &(event_registry[event_id % HASH_TABLE_SIZE]);
    EVENT_HEADER event_header;
    event_header.trig_number = htonl(event_id);
    event_header.device_mask = htonll(event->bit_word);
    event_header.status = htonl((event->bit_word == COMPLETE_EVENT_MASK) ? 0 : 1);
    char* data = NULL;
    int data_len;
    unsigned long long total_bytes_written = 0;

    // First write the event header, the write the FONTUS Trigger DATA;
    // then write the waveform data
    if(!(nwritten = fwrite(&event_header.trig_number, sizeof(event_header.trig_number), 1, fout))) {
        printf("Error writing event header to disk\n");
        goto ERROR;
    }
    total_bytes_written += nwritten*sizeof(event_header.trig_number);

    if(!(nwritten = fwrite(&event_header.status, sizeof(event_header.status), 1, fout))) {
        printf("Error writing event header to disk\n");
        goto ERROR;
    }
    total_bytes_written += nwritten*sizeof(event_header.status);

    if(!(nwritten = fwrite(&event_header.device_mask, sizeof(event_header.device_mask), 1, fout))) {
        printf("Error writing event header to disk\n");
        goto ERROR;
    }
    total_bytes_written += nwritten*sizeof(event_header.device_mask);

    // Write FONTUS Trigger data...
    if((COMPLETE_EVENT_MASK & (1ULL<<FONTUS_DEVICE_ID)) != 0) {
        grab_data_from_pubsub_message(event->data[FONTUS_DEVICE_ID], &data, &data_len);
        if(!data) {
            printf("Error getting FONTUS data\n");
            goto ERROR;
        }
        if(!(nwritten = fwrite(data, data_len, 1, fout))) {
            printf("Error writing FONTUS data to disk\n");
            goto ERROR;
        }
        total_bytes_written += nwritten*data_len;
        fflush(fout);
    }

    // Now write all the remaining (presumably CERES) data
    for(i=0; i<MAX_DEVICE_NUMBER; i++) {
        // if this device number is in the COMPLETE_EVENT_MASK then it should be readout,
        // otherwise I don't care about it.
        if(i==FONTUS_DEVICE_ID || (COMPLETE_EVENT_MASK & (1ULL<<i)) == 0) {
            continue;
        }
        grab_data_from_pubsub_message(event->data[i], &data, &data_len);
        if(!data) {
            printf("Error, bad data in 'complete' event!\n");
            goto ERROR;
            exit(1);
        }
        nwritten = fwrite(data, data_len, 1, fout);
        if(nwritten != 1) {
            // Print errno...
            printf("Error writing event to disk: %s\n", strerror(errno));
            goto ERROR;
        }
        total_bytes_written += data_len*nwritten;
        // TODO, I'm really not sure when it's necessary to fflush things.
        // Does it have to be done before data is freed??  Idk I tried googling
        // it but I couldn't find anything
        fflush(fout);
    }
    return total_bytes_written;

ERROR:;
      // TODO this isn't good error handling lol
      printf("I'm gonna die now\n");
      loop = 0;
      return -1;
}

void free_event(int event_id) {
    int i;
    EventRecord* event = &(event_registry[event_id % HASH_TABLE_SIZE]);

    // Write FONTUS Trigger data...
    if((COMPLETE_EVENT_MASK & (1ULL<<FONTUS_DEVICE_ID)) != 0) {
        freeReplyObject(event->data[FONTUS_DEVICE_ID]);
    }

    // Now write all the remaining (presumably CERES) data
    for(i=0; i<MAX_DEVICE_NUMBER; i++) {
        // if this device number is in the COMPLETE_EVENT_MASK then it should be free'd
        if(i==FONTUS_DEVICE_ID || (COMPLETE_EVENT_MASK & (1ULL<<i)) == 0) {
            continue;
        }
        freeReplyObject(event->data[i]);
    }
}

int send_event_to_redis(redisContext* redis, int event_id) {
    int i;
    redisReply* r;
    size_t arglens[3];
    const char* args[3];

    char* data = NULL;
    int data_len;
    static unsigned char* redis_data_buf = NULL;
    unsigned long offset = 0;

    if(!redis) {
        printf("BAD REDIS\n");
        return 0;
    }
    if(!redis_data_buf) {
        redis_data_buf = malloc(REDIS_OUT_DATA_BUF_SIZE);
        if(!redis_data_buf) {
            printf("Could not allocate memory for redis_data_buffer \n");
            return 0;
        }
    }

    EventRecord* event = &(event_registry[event_id % HASH_TABLE_SIZE]);
    EVENT_HEADER event_header;
    event_header.trig_number = htonl(event_id);
    event_header.device_mask = htonll(event->bit_word);
    event_header.status = htonl((event->bit_word == COMPLETE_EVENT_MASK) ? 0 : 1);

    memcpy(redis_data_buf + offset, &event_header.trig_number, sizeof(event_header.trig_number));
    offset += sizeof(event_header.trig_number);
    memcpy(redis_data_buf + offset, &event_header.device_mask, sizeof(event_header.device_mask));
    offset += sizeof(event_header.device_mask);
    memcpy(redis_data_buf + offset, &event_header.status, sizeof(event_header.status));
    offset += sizeof(event_header.status);

    // Write FONTUS Trigger data...
    if(0 && ((COMPLETE_EVENT_MASK & (1ULL<<FONTUS_DEVICE_ID)) != 0)) {
        grab_data_from_pubsub_message(event->data[FONTUS_DEVICE_ID], &data, &data_len);
        if(!data) {
            return 0;
        }
        memcpy(redis_data_buf + offset, data, data_len);
        offset += data_len;
    }

    // Now write all the remaining (presumably CERES) data
    for(i=0; i<MAX_DEVICE_NUMBER; i++) {
        // if this device number is in the COMPLETE_EVENT_MASK then it should be readout,
        // otherwise I don't care about it.
        if(i==FONTUS_DEVICE_ID || (COMPLETE_EVENT_MASK & (1ULL<<i)) == 0) {
            continue;
        }
        grab_data_from_pubsub_message(event->data[i], &data, &data_len);
        if(!data) {
            return 0;
        }
        memcpy(redis_data_buf + offset, data, data_len);
        offset += data_len;
        // TODO, I'm really not sure when it's necessary to fflush things.
        // Does it have to be done before data is freed??  Idk I tried googling
        // it but I couldn't find anything
    }

    args[0] = "PUBLISH";
    arglens[0] = strlen(args[0]);
    args[1] = "full_event_stream";
    arglens[1] = strlen(args[1]);
    args[2] = (char*)redis_data_buf;
    arglens[2] = offset;

    r = redisCommandArgv(redis, 3,  args,  arglens);
    if(!r) {
        // TODO
        printf("ERROR sending data to redis!\n"); // XXX Non-block
        //builder_log(LOG_ERROR, "Redis error!");
    }

    if(r->type == REDIS_REPLY_STRING || r->type == REDIS_REPLY_ERROR) {
        printf("FUCK %s\n", r->str);
    }
    freeReplyObject(r);
    return  arglens[2];
}

void print_help_string() {
    printf("zipper: recieves then combines data from CERES & FONTUS data builders via redis DB.\n"
            "   usage:  zipper [--out filename] [--mask event_mask]\n");
}

int main(int argc, char** argv) {

    int run_mode;
    unsigned long long file_size_threshold = 0;

    const char * output_filename = "/dev/null";
    struct option clargs[] = {{"out", required_argument, NULL, 'o'},
                              {"mask", required_argument, NULL, 'm'},
                              {"run-mode", no_argument, NULL, 'r'},
                              {"help", no_argument, NULL, 'h'},
                              { 0, 0, 0, 0}};
    int optindex;
    int opt;
    while((opt = getopt_long(argc, argv, "o:m:", clargs, &optindex)) != -1) {
        switch(opt) {
            case 0:
                // Should be here if the option has the "flag" set
                // Currently not possible, but someday I might add
                break;
            case 'o':
                printf("Output data file set to '%s'\n", optarg);
                output_filename = optarg;
                break;
            case 'm':
                COMPLETE_EVENT_MASK = strtoul(optarg, NULL, 0);
                if(!COMPLETE_EVENT_MASK) {
                    printf("Event mask '%s' couldn't be interpreted. Dying\n", optarg);
                    return 0;
                }
                break;
            case 'r':
                return printf("RUN MODE\n");
                file_size_threshold = DEFAULT_FILE_SIZE_THRESHOLD;
                break;
            case 'h':
                print_help_string();
                return 0;
            case '?':
            case ':':
                // This should happen if there's a missing or unknown argument
                // getopt_long outputs its own error message
                return 0;

        }
    }

    printf("COMPLETE EVENT MASK = 0x%lx\n", COMPLETE_EVENT_MASK);

    redisReply* reply = NULL;
    redisContext* redis = NULL;
    //const char* redis_hostname = "127.0.0.1";
    int done;

    //FILE* fout = fopen(output_filename, "wb");
    FILE* fout = fopen(output_filename, "ab");
    if(!fout) {
        printf("Could not open output file '%s'. Data will not be saved!\n", output_filename);
        return 1;
    }
    signal(SIGKILL, signal_handler);
    signal(SIGINT, signal_handler);

    redis = create_redis_unix_conn("/var/run/redis/redis-server.sock", 1);
    redisAppendCommand(redis, "SUBSCRIBE event_stream");
    redisBufferWrite(redis, &done);

    // TODO! need to improve the non-blocking connectivity scheme
    usleep(50000);

    redisBufferRead(redis);
    if(!reply) {
        redisGetReply(redis, (void**)&reply);
    }

    if(!reply) {
        printf("Uh oh, couldn't subscribe to redis...\n");
        return -1;
    }
    freeReplyObject(reply);

    redisContext* publish_redis = create_redis_unix_conn("/var/run/redis/redis-server.sock", 0);
    struct timeval redis_update_time, event_rate_time, current_time;
    gettimeofday(&redis_update_time, NULL); // Initialize previous time to now
    event_rate_time = redis_update_time;
    int redis_is_readable = 1; // TODO this should come from select or something like that
    int built_count = 0;
    double delta_t;
    int event_id = -1;
    int bytes_sent = 0;
    unsigned long long bytes_in_file = 0;
    int nbytes_written;
    int run_number = 0;
    int sub_run_number = 0;
    const char* file_name_template = "%s_r%06i_f%06i.dat";
    char buffer[128];

    printf("Starting main loop\n");
    while(loop) {
        gettimeofday(&current_time, NULL);
        if(redis_is_readable) {
            recieve_waveform_from_redis(redis);
        }
        if(event_ready_queue.events_available) {
            event_id = pop_complete_event_id();
            built_count += 1;
            // TODO save_event and send_to_redis are very similar functions,
            // should see if I can combine them or something like that.

            delta_t = (current_time.tv_sec - redis_update_time.tv_sec)*1e6 + (current_time.tv_usec - redis_update_time.tv_usec);
            if(delta_t > REDIS_COOLDOWN && bytes_sent < PUBLISH_MAX_RATE/10) {
                bytes_sent += send_event_to_redis(publish_redis, event_id);
                redis_update_time = current_time;
            }
            if((nbytes_written = save_event(fout, event_id)) == -1) {

            }
            bytes_in_file += nbytes_written;
            free_event(event_id);

            // Check if it's time to change to a new sub-run
            if(file_size_threshold && bytes_in_file > file_size_threshold) {
                // Time to rotate files
                fflush(fout);
                fclose(fout);

                snprintf(buffer, 128, file_name_template, "jsns_data", run_number, ++sub_run_number);
                fout = fopen(buffer, "ab");
                if(!fout) {
                    printf("Could not open file '%s': %s", buffer, strerror(errno));
                    printf("Events will not be saved!");
                }
                bytes_in_file = 0;
            }
        }
        delta_t = (current_time.tv_sec - event_rate_time.tv_sec)*1e6 + (current_time.tv_usec - event_rate_time.tv_usec);

        // A 10th of a second
        if(delta_t > 100000) {
            bytes_sent = 0;
        }

        if(delta_t > PRINT_UPDATE_COOLDOWN) {
            printf("Event id %i.\t%0.2f events per second.\t%iMB sent to redis\n", event_id, (float)1e6*built_count/PRINT_UPDATE_COOLDOWN, bytes_sent/(1024*1024));
            built_count = 0;
            event_rate_time = current_time;
        }
        if(disconnect_from_redis) {
            printf("Killing redis\n");
            redisFree(redis);
            loop = 0;
        }
        // Just to put a bit of a speed limit on things
        //usleep(100);
    }
    // Clean up
    fclose(fout);
    redisFree(redis);
    printf("Bye\n");
    return 0;
}
