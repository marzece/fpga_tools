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
#define NUM_DATA_STREAMS 2
#define DATA_HEADER_NBYTES 20
#define HASH_TABLE_SIZE 1000
#define COMPLETE_EVENT_MASK 0x300000000ULL
#define QUEUE_LENGTH 100
#define FONTUS_DEVICE_ID 0

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

typedef struct ReadyEventQueue {
    uint32_t event_ids[QUEUE_LENGTH];
    int events_available;
} ReadyEventQueue;
ReadyEventQueue event_ready_queue;

#define MAX_DEVICE_NUMBER 34 // Maximum device ID
typedef struct FullEvent {
    int data_nbytes[MAX_DEVICE_NUMBER];
    redisReply* redis_obj[MAX_DEVICE_NUMBER];
    int counter;
} FullEvent;
// Hash store
typedef struct EventRecord {
    uint64_t bit_word;
    //unsigned int count;
    uint32_t event_number;
    redisReply* data[MAX_DEVICE_NUMBER];
} EventRecord;

EventRecord event_registry[HASH_TABLE_SIZE];

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

uint32_t pop_complete_event() {
    if(event_ready_queue.events_available == 0) {
        printf("Pop when event queue was empty!\n");
        return -1; // TODO, should have a better error system here

    }
    return event_ready_queue.event_ids[--event_ready_queue.events_available];
}

uint32_t last_seen_event[MAX_DEVICE_NUMBER];

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
        printf("Hash collision!\n");
        exit(1);
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

    printf("Got reply\n");
    // I'm almost certain that a redis pub-sub message is always an array
    // The first element of the array is a string that just says "message"
    // The second element is a string that gives the channel name (don't care about that)
    // The third element is that actual data
    assert(reply->type == REDIS_REPLY_ARRAY);
    assert(reply->elements == 3);

    redisReply* rr_dat = reply->element[2];
    assert(rr_dat->type == REDIS_REPLY_STRING);
    assert(rr_dat->len >= DATA_HEADER_NBYTES); // Header should always be 20 bytes

    uint32_t event_number = htonl(*((uint32_t*) (rr_dat->str+4)));
    uint32_t device_id = *((uint8_t*) (rr_dat->str+18));
    printf("%i %i\n", event_number, device_id);

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
    if(!fwrite(&event_header.device_mask, sizeof(event_header.device_mask), 1, fout)) {
        printf("Error writing event header to disk\n");
        goto ERROR;
    }
    if(!fwrite(&event_header.status, sizeof(event_header.status), 1, fout)) {
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
        fwrite(&data, data_len, 1, fout);
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
