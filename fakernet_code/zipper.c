#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include "hiredis/hiredis.h"

// The number of seperate data streams, each of which must
// be present for an event to be complete.
#define MAX_DEVICE_NUMBER 34 // Maximum device ID
#define DATA_HEADER_NBYTES 20
#define HASH_TABLE_SIZE 1000
//#define COMPLETE_EVENT_MASK 0xFF0ULL
#define COMPLETE_EVENT_MASK 0x30ULL
#define QUEUE_LENGTH 100
#define FONTUS_DEVICE_ID 1
#define REDIS_OUT_DATA_BUF_SIZE (32*1024*1024)

uint32_t last_seen_event[MAX_DEVICE_NUMBER];
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
    uint64_t beam_time; 
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

redisContext* create_redis_conn(const char* redis_hostname) {
    printf("Opening Redis Connection\n");
    redisContext* c;
    c = redisConnect(redis_hostname, 6379);
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
        printf("Cntrl-C caught\n");
        disconnect_from_redis = 1;
        num_kills +=1;
    }
    if(num_kills == 2) {
        loop = 0;
        exit(1);
    }
    if(num_kills > 2) {
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

uint32_t pop_complete_event_id() {
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

void recieve_waveform__from_redis(redisContext* redis) {
    redisReply* reply;
    if(redisBufferRead(redis) != REDIS_OK) {
        // Error
        printf("REDIS BUF READ ERROR\n");
        exit(1);

    }
    if(redisGetReplyFromReader(redis, (void**)&reply) != REDIS_OK) {
        printf("REDIS BUF READ ERROR\n");
        exit(1);
        // Error
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
    //freeReplyObject(reply);
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

void free_event_data(FullEvent* event) {
    int i;
    for(i=0; i<MAX_DEVICE_NUMBER; i++) {
        freeReplyObject(event->redis_obj[i]);
    }
    memset(event, 0, sizeof(FullEvent));
}

void save_event(FILE* fout, int event_id) {
    // TODO save_event free's the memory for the event, it probably shouldn't
    // do that b/c it's not obvious. Should have a specific function handle that.
    int i;
    int nwritten;
    EventRecord* event = &(event_registry[event_id % HASH_TABLE_SIZE]);
    EVENT_HEADER event_header;
    event_header.trig_number = htonl(event_id);
    event_header.device_mask = htonll(event->bit_word);
    event_header.status = htonl((event->bit_word == COMPLETE_EVENT_MASK) ? 0 : 1);
    char* data = NULL;
    int data_len;

    // First write the event header, the write the FONTUS Trigger DATA;
    // then write the waveform data
    if(!fwrite(&event_header.trig_number, sizeof(event_header.trig_number), 1, fout)) {
        printf("Error writing event header to disk\n");
        goto ERROR;
    }
    if(!fwrite(&event_header.status, sizeof(event_header.status), 1, fout)) {
        printf("Error writing event header to disk\n");
        goto ERROR;
    }
    if(!fwrite(&event_header.device_mask, sizeof(event_header.device_mask), 1, fout)) {
        printf("Error writing event header to disk\n");
        goto ERROR;
    }

    // Write FONTUS Trigger data...
    if((COMPLETE_EVENT_MASK & (1ULL<<FONTUS_DEVICE_ID)) != 0) {
        // TODO
        grab_data_from_pubsub_message(event->data[FONTUS_DEVICE_ID], &data, &data_len);
        if(!data) {
            printf("Error getting FONTUS data\n");
            goto ERROR;
        }
        fwrite(data, data_len, 1, fout);
        fflush(fout);
        freeReplyObject(event->data[FONTUS_DEVICE_ID]);
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
        // TODO, I'm really not sure when it's necessary to fflush things.
        // Does it have to be done before data is freed??  Idk I tried googling
        // it but I couldn't find anything
        fflush(fout);
        freeReplyObject(event->data[i]);
    }
    return;

ERROR:;
      // TODO this isn't good error handling lol
      printf("I'm gonna die now\n");
      loop = 0;
      return;
}

void send_event_to_redis(redisContext* redis, int event_id) {
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
        return;
    }
    if(!redis_data_buf) {
        redis_data_buf = malloc(REDIS_OUT_DATA_BUF_SIZE);
        if(!redis_data_buf) {
            printf("Could not allocate memory for redis_data_buffer \n");
            return;
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
    if((COMPLETE_EVENT_MASK & (1ULL<<FONTUS_DEVICE_ID)) != 0) {
        printf("WRITING FONTUS DATA\n");
        // TODO
        grab_data_from_pubsub_message(event->data[FONTUS_DEVICE_ID], &data, &data_len);
        if(!data) {
            return;
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
            return;
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
    if(r->type == REDIS_REPLY_STRING || r->type == REDIS_REPLY_ERROR) {
        printf("FUCK %s\n", r->str);
    }
    if(!r) {
        // TODO
        printf("ERROR sending data to redis!\n");
        //builder_log(LOG_ERROR, "Redis error!");
    }
    freeReplyObject(r);
}

int main() {
    // First 
    redisReply* reply;
    redisContext* redis = NULL;
    const char* redis_hostname = "127.0.0.1";

    FILE* fout = fopen("COMBINED_DATA_TEST.dat", "wb");
    signal(SIGKILL, signal_handler);
    signal(SIGINT, signal_handler);

    redis = create_redis_conn(redis_hostname);
    reply = redisCommand(redis, "SUBSCRIBE event_stream");

    if(!reply) {
        printf("Uh oh, couldn't subscribe to redis...\n");
        return -1;
    }
    freeReplyObject(reply);

    redisContext* publish_redis = create_redis_conn(redis_hostname);

    int redis_is_readable = 1; // TODO this should come from select or something like that
    printf("Starting main loop\n");
    //FullEvent event;
    while(loop) {
        if(disconnect_from_redis) {
            printf("Killing redis\n");
            redisFree(redis);
        }
        else if(redis_is_readable) {
            recieve_waveform__from_redis(redis);
        }
        if(event_ready_queue.events_available) {
            int event_id = pop_complete_event_id();
            printf("Event %i is done\n", event_id);
            // TODO save_event and send_to_redis are very similar functions,
            // should see if I can combine them or something like that.
            send_event_to_redis(publish_redis, event_id);
            save_event(fout, event_id);
            //grab_full_event(redis, event_id, &event);
            //save_event(fout, &event);
            //free_event_data(&event);
        }
    }
    // Clean up
    fclose(fout);
    redisFree(redis);
    return 0;
}
