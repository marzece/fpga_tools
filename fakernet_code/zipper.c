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
#include "util.h"
#include "hiredis/hiredis.h"
#include "daq_logger.h"

#define DATA_FORMAT_VERSION 1

// The number of seperate data streams, each of which must
// be present for an event to be complete.
#define MAX_DEVICE_NUMBER 34 // Maximum device ID
#define DATA_HEADER_NBYTES 20
#define HASH_TABLE_SIZE 1000
#define DEFAULT_DATA_OUT_FILE "/dev/null"
#define DEFAULT_EVENT_MASK 0xFF1ULL

#define QUEUE_LENGTH 100
#define FONTUS_DEVICE_ID 0
#define REDIS_UNIX_SOCK_PATH "/var/run/redis/redis-server.sock"
//#define REDIS_UNIX_SOCK_PATH "/Users/marzece/redis-server.sock"
#define REDIS_OUT_DATA_BUF_SIZE (32*1024*1024)
#define DEFAULT_FILE_SIZE_THRESHOLD (1024*1024*1024ULL) // 1GB

#define DEFAULT_PUBLISH_RATE 10 // Hz
#define DEFAULT_PUBLISH_MAX_SIZE (10*1024*1024) // 10 MB
#define LOG_REDIS_HOST_NAME "localhost"
#define PRINT_UPDATE_COOLDOWN 10000000 // How often the status is printed to the terminal/log
#define LOG_MESSAGE_MAX 1024
#define DAQ_LOG_NAME "data-zipper"
#define DEFAULT_LOG_FILENAME "zipper_error_log.log"

// How often should stats be published to the redis data base in micro-seconds
#define REDIS_STATS_COOLDOWN 1e6

// For some reason linux doesn't always have htonll...so this can be used instead
#if __linux__
#define htonll(x) ((((uint64_t)htonl(x)) << 32) + htonl((x) >> 32))
#define ntohll(x) htonll(x)
#endif

uint32_t last_seen_event[MAX_DEVICE_NUMBER];
uint64_t COMPLETE_EVENT_MASK = DEFAULT_EVENT_MASK;

// Global variable to stop the main program loop.
// Set inside a signal handler, thus the weird data type.
volatile sig_atomic_t loop = 1;

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
    uint16_t status;
    uint16_t version;
    uint64_t device_mask;
} EVENT_HEADER;

typedef struct FullEvent {
    int data_nbytes[MAX_DEVICE_NUMBER];
    redisReply* redis_obj[MAX_DEVICE_NUMBER];
    int counter;
} FullEvent;

// TODO this should be defined in some common header file
typedef struct TrigHeader {
    uint32_t magic_number;
    uint32_t trig_number;
    uint64_t clock;
    uint16_t length;
    uint8_t device_id;
    uint8_t crc;
} TrigHeader;

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

typedef struct RunInfo {
    long long run_number;
    long long sub_run;
} RunInfo;

typedef struct ProcessingStats {
    unsigned int event_count; // Number of events built (since program startup)
    unsigned int trigger_id; // Most recent event's trigger_id
    unsigned int device_mask;
    unsigned int events_waiting; // Events sitting around in the buffer
    unsigned long long latest_timestamp; // Most recent event's clock timestamp
    unsigned long long max_delta_t; // Largest difference in times stamps observed
    long long fontus_delta_t; // Time difference between FONTUS timestamp & latest CERES timetstamp
    long long run_number; // Current run number
    long long sub_run_number; // Current sub-run number
    double start_time; // In microseconds (since Epoch start)
    double uptime; // In microseconds
    unsigned int pid; // PID for this program
} ProcessingStats;

void initialize_processing_stats(ProcessingStats* stats) {
    if(!stats) {
        return;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);

    stats->event_count = 0;
    stats->trigger_id = 0;
    stats->device_mask = 0;
    stats->events_waiting = 0;
    stats->latest_timestamp = 0;
    stats->max_delta_t = 0;
    stats->fontus_delta_t = 0;
    stats->run_number = -1;
    stats->start_time = tv.tv_sec*1e6 + tv.tv_usec;
    stats->uptime = 0;
    stats->pid = getpid();
}

redisContext* create_redis_conn(const char* redis_hostname, int port) {
    daq_log(LOG_INFO, "Opening Redis Connection");
    redisContext* c;
    c = redisConnect(redis_hostname, port);
    if(c == NULL || c->err) {
        daq_log(LOG_ERROR, "Redis connection error %s", c->errstr);
        redisFree(c);
        return NULL;
    }
    return c;
}

redisContext* create_redis_unix_conn(const char* path, int nonblock) {
    daq_log(LOG_INFO, "Opening Redis Connection");
    redisContext* c;
    if(!nonblock) {
        c = redisConnectUnix(path);
    }  else {
        c = redisConnectUnixNonBlock(path);
    }
    if(c == NULL || c->err) {
        daq_log(LOG_ERROR, "Redis connection error %s", c->errstr);
        redisFree(c);
        return NULL;
    }
    return c;
}

void signal_handler(int signum) {
    // TODO think of more signals that would be useful
    static int num_kills = 0;
    if(num_kills >= 1) {
        daq_log(LOG_WARN, "Dying...");
        exit(1);
    }
    if(signum == SIGINT || signum == SIGKILL) {
        daq_log(LOG_WARN, "Cntrl-C caught. Will attempt graceful exit. Cntrl-C again for immediate exit.");
        loop = 0;
        num_kills +=1;
    }
}

void flag_complete_event(uint32_t event_number) {

    if(event_ready_queue.events_available == QUEUE_LENGTH) {
        // TODO figure out how this could be handled
        daq_log(LOG_ERROR, "QUEUE FULL! Events not being saved!");
        return;
    }
    event_ready_queue.event_ids[event_ready_queue.events_available++] = event_number;
}

uint32_t pop_complete_event_id(void) {
    if(event_ready_queue.events_available == 0) {
        daq_log(LOG_ERROR, "Trying to pop empty queue. That shouldn't happen");
        return -1;
    }
    return event_ready_queue.event_ids[--event_ready_queue.events_available];
}

static inline unsigned  int event_id_hash(uint32_t event_id) {
    return event_id % HASH_TABLE_SIZE;
}

void register_waveform(uint32_t device_id, uint32_t event_number, redisReply* wf_data) {
    // Device number 0-3 (inclusive) are taken by the two FONTUS boards,
    // so device number 4 is the first CERES board.
    //size_t i;

    // If the waveform isn't part of the event mask just ignore it
    if(((1<<device_id) & COMPLETE_EVENT_MASK) == 0) {
        // Toss the data, we're not gonna use it (perhaps should warn user?)
        freeReplyObject(wf_data);
        return;
    }

    unsigned int hash_value = event_id_hash(event_number);
    if(event_number == last_seen_event[device_id]+1) {
        // Pass
    }
    else if(event_number > last_seen_event[device_id]+1) {
    }
    else {
        // Event comes before previous event. Unclear how this happened and how
        // it should be handled...
        daq_log(LOG_WARN, "Event skipped backwards!");
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
        daq_log(LOG_WARN, "Hash collision!");
        //exit(1);
        // TODO, figure out how this should be handled
    }

    if(event_registry[hash_value].bit_word == COMPLETE_EVENT_MASK) {
        flag_complete_event(event_number);
    }
}

void recieve_waveform_from_redis(redisContext* redis) {
    redisReply* reply;
    if(redisBufferRead(redis) != REDIS_OK) {
        // Error
        daq_log(LOG_ERROR, "REDIS BUF READ ERROR1: %s", redis->errstr);
        exit(1);
    }

    // Process as much data in the receive buffer as is available
    while(1) {
        if(redisGetReplyFromReader(redis, (void**)&reply) != REDIS_OK) {
            // Error
            daq_log(LOG_ERROR, "REDIS BUF READ ERROR2: %s", redis->errstr);
            exit(1);
        }

        if(!reply) {
            // No data available. We're done here
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
    EventRecord* event = &(event_registry[event_id_hash(event_id)]);
    EVENT_HEADER event_header;
    event_header.trig_number = htonl(event_id);
    event_header.device_mask = htonll(event->bit_word);
    event_header.status = htons((event->bit_word == COMPLETE_EVENT_MASK) ? 0 : 1);
    event_header.version = htons(DATA_FORMAT_VERSION);
    char* data = NULL;
    int data_len;
    unsigned long long total_bytes_written = 0;

    // First write the event header, the write the FONTUS Trigger DATA;
    // then write the waveform data
    if(!(nwritten = fwrite(&event_header.trig_number, sizeof(event_header.trig_number), 1, fout))) {
        daq_log(LOG_ERROR, "Error writing event header to disk");
        goto ERROR;
    }
    total_bytes_written += nwritten*sizeof(event_header.trig_number);

    if(!(nwritten = fwrite(&event_header.status, sizeof(event_header.status), 1, fout))) {
        daq_log(LOG_ERROR, "Error writing event header to disk");
        goto ERROR;
    }
    total_bytes_written += nwritten*sizeof(event_header.status);

    if(!(nwritten = fwrite(&event_header.version, sizeof(event_header.version), 1, fout))) {
        daq_log(LOG_ERROR, "Error writing event header to disk");
        goto ERROR;
    }
    total_bytes_written += nwritten*sizeof(event_header.version);

    if(!(nwritten = fwrite(&event_header.device_mask, sizeof(event_header.device_mask), 1, fout))) {
        daq_log(LOG_ERROR, "Error writing event header to disk");
        goto ERROR;
    }
    total_bytes_written += nwritten*sizeof(event_header.device_mask);

    // Write FONTUS Trigger data...
    if((COMPLETE_EVENT_MASK & (1ULL<<FONTUS_DEVICE_ID)) != 0) {
        grab_data_from_pubsub_message(event->data[FONTUS_DEVICE_ID], &data, &data_len);
        if(!data) {
            daq_log(LOG_ERROR, "Error getting FONTUS data");
            goto ERROR;
        }
        if(!(nwritten = fwrite(data, data_len, 1, fout))) {
            daq_log(LOG_ERROR, "Error writing FONTUS data to disk");
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
            daq_log(LOG_ERROR, "Error, bad data in 'complete' event!");
            goto ERROR;
            exit(1);
        }
        nwritten = fwrite(data, data_len, 1, fout);
        if(nwritten != 1) {
            // Print errno...
            daq_log(LOG_ERROR, "Error writing event to disk: %s", strerror(errno));
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
      daq_log(LOG_ERROR, "I'm gonna die now");
      loop = 0;
      return -1;
}

void free_event(int event_id) {
    int i;
    const unsigned int hash_value = event_id_hash(event_id);
    EventRecord* event = &(event_registry[hash_value]);

    // Clear the event registry for this event
    // Setting the bit_word to zero basically flags the event as
    // "new" therefore making the entry valid for future events
    event_registry[hash_value].bit_word = 0;

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
    if(!redis) {
        return 0;
    }
    int i;
    size_t arglens[3];
    const char* args[3];

    char* data = NULL;
    int data_len;
    static unsigned char* redis_data_buf = NULL;
    unsigned long offset = 0;
    int done = 0;

    if(!redis_data_buf) {
        redis_data_buf = malloc(REDIS_OUT_DATA_BUF_SIZE);
        if(!redis_data_buf) {
            daq_log(LOG_ERROR, "Could not allocate memory for redis_data_buffer");
            return 0;
        }
    }

    EventRecord* event = &(event_registry[event_id % HASH_TABLE_SIZE]);
    EVENT_HEADER event_header;
    event_header.trig_number = htonl(event_id);
    event_header.status = htonl((event->bit_word == COMPLETE_EVENT_MASK) ? 0 : 1);
    event_header.version = htons(DATA_FORMAT_VERSION);
    event_header.device_mask = htonll(event->bit_word);

    memcpy(redis_data_buf + offset, &event_header.trig_number, sizeof(event_header.trig_number));
    offset += sizeof(event_header.trig_number);
    memcpy(redis_data_buf + offset, &event_header.status, sizeof(event_header.status));
    offset += sizeof(event_header.status);
    memcpy(redis_data_buf + offset, &event_header.version, sizeof(event_header.version));
    offset += sizeof(event_header.version);
    memcpy(redis_data_buf + offset, &event_header.device_mask, sizeof(event_header.device_mask));
    offset += sizeof(event_header.device_mask);

    // Write FONTUS Trigger data...
    if((COMPLETE_EVENT_MASK & (1ULL<<FONTUS_DEVICE_ID)) != 0) {
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

    redisAppendCommandArgv(redis, 3,  args,  arglens);
    // TODO should I add some loop counter so I don't get stuck here?
    while(!done) {
        redisBufferWrite(redis, &done);
    }
    return  arglens[2];
}

void redis_publish_stats(redisContext* c, const ProcessingStats* stats) {
    if(!c || !stats) {
        return;
    }
    size_t arglens[3];
    const char* args[3];
    int done = 0;
    char buf[2048];

    // Use a a Stream instead of a pub-sub for this?
    // Or maybe just a key-value store?
    args[0] = "PUBLISH";
    arglens[0] = strlen(args[0]);

    args[1] = "zipper_stats";
    arglens[1] = strlen(args[1]);

    arglens[2] = snprintf(buf, 2048, "%lli %lli %u %u %llu %llu %lli %u %i %i %i",
                                                          stats->run_number,
                                                          stats->sub_run_number,
                                                          stats->event_count,
                                                          stats->trigger_id,
                                                          stats->latest_timestamp,
                                                          stats->max_delta_t,
                                                          stats->fontus_delta_t,
                                                          stats->device_mask,
                                                          stats->events_waiting,
                                                          stats->pid,
                                                          (int)(stats->uptime/1e6));

    args[2] = buf;
    redisAppendCommandArgv(c, 3,  args,  arglens);
    // TODO should I add some loop counter so I don't get stuck here?
    while(!done) {
        redisBufferWrite(c, &done);
    }
}

int wait_for_redis_readable(const redisContext* r, int timeout) {
    fd_set readfds;
    struct timeval _timeout;
    _timeout.tv_sec = timeout / 1000000;
    _timeout.tv_usec = timeout % 1000000;
    FD_ZERO(&readfds);
    FD_SET(r->fd, &readfds);
    return select(r->fd+1, &readfds, NULL, NULL, &_timeout);
}

RunInfo parse_run_info_redis_reply(redisReply* reply, int skip_steps) {
    // TODO replace all the asserts here with goto ERROR type things
    RunInfo ret;
    memset(&ret, 0, sizeof(RunInfo));

    // Got run info, have to parse the array
    // First thing should just be a list of all the responses
    // there should only be one

    redisReply* stream_data;
    // If XREVRANGE was called instead of XREAD we can skip a few stpes
    if(!skip_steps) {
        assert(reply->elements == 1);
        // Then next element down
        redisReply* stream_info = reply->element[0];
        assert(stream_info->type == REDIS_REPLY_ARRAY && stream_info->elements == 2);
        //The first element is the stream name "run_info"
        //The second element is the actual new data
        stream_data = stream_info->element[1];

    }
    else {
        stream_data = reply;
    }
    // The data is array that should be of length 1, but
    // can be longer if several new runs were submitted at the same
    // time...not sure why that would happen, but it's possible.
    // Either way, we take the last record
    assert(stream_data->type==REDIS_REPLY_ARRAY);
    redisReply* record = stream_data->element[reply->elements-1];

    // This SHOULD be a record of length 2
    // The first value is the redis KEY,we'll store that for later use.
    // The second value is the values
    assert(record->type == REDIS_REPLY_ARRAY && record->elements == 2);
    redisReply* key = record->element[0];
    assert(key->type == REDIS_REPLY_STRING);
    // TODO this should be a memcpy/malloc or something like that
    // so that I can free() the reply safely
    //char* last_key = key->str;

    redisReply* values = record->element[1];
    assert(values->type==REDIS_REPLY_ARRAY && values->elements >= 2);

    unsigned int i;
    for(i=0; i<values->elements; i+=2) {
        // Values come in key-value pairs
        redisReply* k = values->element[i];
        redisReply* v = values->element[i+1];
        assert(k->type == REDIS_REPLY_STRING);

        if(strncmp("run_number", k->str, k->len) == 0) {
            if(string2ll(v->str, v->len, &ret.run_number) == 0) {
                daq_log(LOG_ERROR, "Cannot parse run number from redis. "
                                   "Will default to RUN=0");
                ret.run_number = 0;
            }
        }
        else if(strncmp("sub_run", k->str, k->len) == 0) {
            if(string2ll(v->str, v->len, &ret.sub_run) == 0) {
                // TODO not sure what I should do here
                daq_log(LOG_ERROR, "Cannot parse sub_run number from redis. "
                                   "Will default to 0");

                ret.sub_run = 0;
            }
        }
        // TODO maybe wanna add a run_type so I can throw into the
        // output data or something like that.
    }
    return ret;
}

void evaluate_event_stats(ProcessingStats* stats, const uint32_t event_id) {
    // I use the "stats" to monitor this program, so I want to get some data
    // about each event so I can ensure the over all data quality
    // So here I just take a look at each event as it comes through and make
    // sure everything is okay

    // Need to get the most recent timestamp
    // Then get all the timestamps and compare their largest difference

    char* data = NULL;
    int data_len;
    int i;
    uint64_t timestamp = 0;
    uint64_t largest_timestamp = 0;
    uint64_t smallest_timestamp = 0;
    uint64_t delta_t;
    uint64_t event_mask= COMPLETE_EVENT_MASK;
    uint64_t fontus_timestamp;
    EventRecord* event = &(event_registry[event_id % HASH_TABLE_SIZE]);

    for(i=0; event_mask; event_mask>>=1,i++) {
        if((event_mask & 0x1) == 0) {
            continue;
        }
        grab_data_from_pubsub_message(event->data[i], &data, &data_len);
        if(!data) {
            continue;
        }
        if(i==FONTUS_DEVICE_ID) {
            fontus_timestamp = ntohll(*((uint64_t*)(data+8)));

        }
        else {
            // A CERES board
            // timestamp occurs at byte offset 8 in the CERES header
            timestamp = ntohll(*((uint64_t*)(data + 8)));
            if(largest_timestamp == 0 || timestamp > largest_timestamp) {
                largest_timestamp = timestamp;
            }
            if(smallest_timestamp == 0 || timestamp < smallest_timestamp) {
                smallest_timestamp = timestamp;
            }
        }
    }

    stats->trigger_id = event_id;
    if(largest_timestamp) {
        delta_t = largest_timestamp - smallest_timestamp;
        if(delta_t > stats->max_delta_t) {
            stats->max_delta_t = largest_timestamp - smallest_timestamp;
        }
        stats->latest_timestamp = largest_timestamp;
    }
    if(largest_timestamp && fontus_timestamp) {
        long long fontus_delta_t = largest_timestamp - fontus_timestamp;
        stats->fontus_delta_t = llabs(fontus_delta_t) > llabs(stats->fontus_delta_t) ? fontus_delta_t : stats->fontus_delta_t;
    }
}

void print_help_string(void) {
    printf("zipper: recieves then combines data from CERES & FONTUS data builders via redis DB.\n"
            "\tusage:  zipper [-o filename] [-m event_mask] [-l log-filename] [--rate rate] [--run-mode] [-v] [-q]\n"
            "\targuments:\n"
            "\t--out -o\tFile to write built data to. Default is '%s'\n"
            "\t--mask -m\tBit mask corresponding to a complete event. Default 0x%llX.\n"
            "\t--log-file -l\tFilename that log messages should be recorded to. Default '%s'\n"
            "\t--rate -r\tMax publish rate in Hz. [NOT IMPLEMENTED!]\n"
            "\t--verbose -v\tIncrease verbosity. Can be done multiple times.\n"
            "\t--quiet -q\tDecrease verbosity. Can be done multiple times.\n"
            "\t--run-mode\tOperate in run-mode. Will recieve run updates from redis. Default off\n",
          DEFAULT_DATA_OUT_FILE, DEFAULT_EVENT_MASK, DEFAULT_LOG_FILENAME);
}

// Helper function, calculates the difference between two timevals in micro-seconds
double calculate_delta_t(struct timeval lhs, struct timeval rhs) {
        return (lhs.tv_sec - rhs.tv_sec)*1e6 + (lhs.tv_usec - rhs.tv_usec);
}

int main(int argc, char** argv) {

    int run_mode = 0;
    int start_new_run = 0;
    redisContext* run_info_redis = NULL;
    redisContext* data_redis = NULL;
    redisContext* publish_redis = NULL;
    redisReply* reply = NULL;
    unsigned long long file_size_threshold = 0;
    const char * output_filename = DEFAULT_DATA_OUT_FILE;
    double publish_rate = DEFAULT_PUBLISH_RATE;
    int built_count = 0;
    double delta_t;
    int event_id = -1;
    int bytes_sent = 0;
    int print_status_bytes_sent;
    unsigned long long bytes_in_file = 0;
    int nbytes_written;
    RunInfo run_info;
    int resume_last_run = 0;
    struct timeval redis_update_time, event_rate_time, byte_sent_time, current_time;
    const char* MDAQ_FN_PREFIX = "jsns2_mdaq";
    const char* file_name_template = "%s.r%06i.f%06i.dat";
    const char* log_filename = DEFAULT_LOG_FILENAME;
    char buffer[128];
    double last_status_update_time = 0;
    ProcessingStats stats;

    run_info.run_number = -1;
    run_info.sub_run = 0;
    int arg_run_mode = 0;

    int verbosity_stdout = LOG_INFO;
    int verbosity_redis = LOG_WARN;
    int verbosity_file = LOG_WARN;

    struct option clargs[] = {{"out", required_argument, NULL, 'o'},
                              {"mask", required_argument, NULL, 'm'},
                              {"run-mode", no_argument, &arg_run_mode, 1},
                              {"log-file", required_argument, NULL, 'l'},
                              {"verbose", no_argument, NULL, 'v'},
                              {"rate", required_argument, NULL, 'r'},
                              {"help", no_argument, NULL, 'h'},
                              { 0, 0, 0, 0}};
    int optindex;
    int opt;
    while((opt = getopt_long(argc, argv, "o:m:r:l:vq", clargs, &optindex)) != -1) {
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
                    printf("Event mask '%s' couldn't be interpreted.\n", optarg);
                    return 0;
                }
                break;
            case 'r':
                publish_rate = atof(optarg);
                printf("Publish rate set to %0.2f\n", publish_rate);
                break;
            case 'v':
                // Reduce the threshold on all the verbosity levels
                verbosity_stdout = verbosity_stdout-1 < LOG_NEVER ? verbosity_stdout-1 : LOG_NEVER;
                verbosity_file = verbosity_file-1 < LOG_NEVER ? verbosity_file-1 : LOG_NEVER;
                verbosity_redis = verbosity_redis-1 < LOG_NEVER ? verbosity_redis-1 : LOG_NEVER;
                break;
            case 'q':
                // Raise the threshold on all the verbosity levels
                verbosity_stdout = verbosity_stdout+1 > LOG_ERROR ? verbosity_stdout-1 : LOG_ERROR;
                verbosity_file = verbosity_file+1 < LOG_ERROR ? verbosity_file+1 : LOG_ERROR;
                verbosity_redis = verbosity_redis+1 < LOG_ERROR ? verbosity_redis+1 : LOG_ERROR;
                break;
            case 'l':
                printf("Log file set to %s\n", optarg);
                log_filename = optarg;
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
        if(arg_run_mode) {
            printf("RUN MODE\n");
            file_size_threshold = DEFAULT_FILE_SIZE_THRESHOLD;
            run_mode = 1;
            break;
        }
    }

    setup_logger(DAQ_LOG_NAME, LOG_REDIS_HOST_NAME, log_filename,
                 verbosity_stdout, verbosity_file, verbosity_redis,
                 LOG_MESSAGE_MAX);
    the_logger->add_newlines = 1;


    // Print the configuration
    daq_log(LOG_WARN, "Data zipper running.\n"
                      "Data file='%s'\n"
                      "Complete event mask = 0x%"PRIx64".\n"
                      "Log file='%s'\n"
                      "Run Mode: %s",
            output_filename, COMPLETE_EVENT_MASK, log_filename, run_mode ? "ON" : "OFF");

    initialize_processing_stats(&stats);
    stats.device_mask = COMPLETE_EVENT_MASK;

    FILE* fout = fopen(output_filename, "ab");
    if(!fout) {
        daq_log(LOG_ERROR, "Could not open output file '%s'. Data will not be saved!", output_filename);
        return 1;
    }
    // TODO, should use sigaction instead of signal
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    data_redis = create_redis_unix_conn(REDIS_UNIX_SOCK_PATH, 0);
    if(!data_redis) {
        daq_log(LOG_ERROR, "Could not connect to redis for receiving data");
        return 1;
    }


    publish_redis = create_redis_unix_conn(REDIS_UNIX_SOCK_PATH, 1);
    if(!publish_redis) {
        daq_log(LOG_ERROR, "Could not connect to redis for publishing data");
    }

    // Connect to redis so I can get run info
    if(run_mode) {
        run_info_redis = create_redis_unix_conn(REDIS_UNIX_SOCK_PATH, 1);
        if(!run_info_redis) {
            daq_log(LOG_ERROR, "Could not connect to redis for run info. Will be using default run 0.");
        }
        else if(resume_last_run) {
            // Get the last run
            redisAppendCommand(run_info_redis, "XREVRANGE run_info + - COUNT 1");
            redisBufferWrite(run_info_redis, NULL);

            if(wait_for_redis_readable(run_info_redis, (int)1e6) > 0) {
                // Socket is readable
                redisBufferRead(run_info_redis);
                redisGetReply(run_info_redis, (void**)&reply);
                if(!reply) {
                    // I'm not sure why this would happen?
                    // We got some reply but it was not well formed?

                }
                else if(reply->type == REDIS_REPLY_ERROR) {

                }
                else if(reply->type == REDIS_REPLY_NIL) {
                    // No runs have been put in the stream


                } else if(reply->type == REDIS_REPLY_ARRAY) {
                    run_info = parse_run_info_redis_reply(reply, 1);
                    daq_log(LOG_INFO, "Got initial run info. "
                                      "Run = %lli-%lli\n", run_info.run_number, run_info.sub_run);
                }
                else {
                    // The reply is neither an error, nor empty, nor an array
                    // I'm not sure how you end up here...this really should
                    // never happen.
                    daq_log(LOG_WARN,"Unexpected response from run_info response. "
                                     "Will be ignoring this response. "
                                     "Type = %i\n", reply->type);
                }

                freeReplyObject(reply);
            }
            else {
                // Either an error happened while waiting for reply,
                // or the timeout expired (which probably is an error too)
                daq_log(LOG_ERROR, "Response timed out while getting initial run info. "
                                   "Will be default to Run 0-0");
            }
        } else {
            daq_log(LOG_INFO, "Starting at RUN 0-0");
        }
        if(run_info_redis) {
            redisAppendCommand(run_info_redis, "XREAD BLOCK 0 STREAMS run_info $");
            redisBufferWrite(run_info_redis, NULL);
        }
    }

    // Initialize some of the timers
    gettimeofday(&redis_update_time, NULL);
    event_rate_time = redis_update_time;
    byte_sent_time = redis_update_time;

    redisAppendCommand(data_redis, "SUBSCRIBE event_stream");
    redisBufferWrite(data_redis, NULL);
    // Need to get the welcome message
    if(wait_for_redis_readable(data_redis, 1000000) > 0) {
        redisBufferRead(data_redis);
        redisGetReply(data_redis, (void**)&reply);
        freeReplyObject(reply);
    }

    daq_log(LOG_INFO, "Starting main loop");
    while(loop) {
        gettimeofday(&current_time, NULL);

        // TODO, should check for error (return = -1) instead of just >0
        if(wait_for_redis_readable(data_redis, 50000) > 0) {
            recieve_waveform_from_redis(data_redis);
        }

        if(run_info_redis && wait_for_redis_readable(run_info_redis, 0) > 0) {
            redisBufferRead(run_info_redis);
            redisGetReply(run_info_redis, (void**)&reply);
            RunInfo new_run_info = parse_run_info_redis_reply(reply, 0);
            // TODO need a better way of handling bad run info
            if(new_run_info.run_number != 0 && new_run_info.run_number != run_info.run_number) {
                run_info = new_run_info;
                start_new_run = 1;
                daq_log(LOG_WARN, "GOT NEW RUN %lli-%lli", run_info.run_number, run_info.sub_run);
            }

            // Ask for next run, block until it arrives
            redisAppendCommand(run_info_redis, "XREAD BLOCK 0 STREAMS run_info $");
            redisBufferWrite(run_info_redis, NULL);
        }

        if(wait_for_redis_readable(publish_redis, 0) > 0) {
            // When publishing data the redis-db will respond
            // I don't care about those responses, but I need to handle them anyways
            do {
                redisBufferRead(publish_redis);
                redisGetReply(publish_redis, (void**)&reply);

                if(reply && (reply->type == REDIS_REPLY_STRING || reply->type == REDIS_REPLY_ERROR)) {
                    daq_log(LOG_ERROR, "ERROR sending data to redis: %s", reply->str);
                }
                freeReplyObject(reply);
            } while(reply);
        }

        if(event_ready_queue.events_available) {
            event_id = pop_complete_event_id();
            check_timestamp(event_id);
            built_count += 1;
            stats.event_count += 1;
            evaluate_event_stats(&stats, event_id);

            if(publish_rate && publish_redis) {
                delta_t = calculate_delta_t(current_time, redis_update_time);
                delta_t /= 1e6;
                if(delta_t > 1.0/publish_rate && bytes_sent < DEFAULT_PUBLISH_MAX_SIZE/10.) {
                    bytes_sent += send_event_to_redis(publish_redis, event_id);
                    redis_update_time = current_time;
                }
            }

            if((nbytes_written = save_event(fout, event_id)) == -1) {
                // TODO shold try and recover from an error instead of dying
                continue;
            }

            bytes_in_file += nbytes_written;
            free_event(event_id);

            // Check if it's time to change to a new sub-run
            if(start_new_run || (file_size_threshold && bytes_in_file > file_size_threshold)){
                // Time to rotate files
                fflush(fout);
                fclose(fout);

                snprintf(buffer, 128, file_name_template, MDAQ_FN_PREFIX, run_info.run_number, ++run_info.sub_run);
                if(run_info.run_number == -1) {
                    // -1 is the "NULL" run number
                    // The only difference is we over write anything that came before
                    fout = fopen(DEFAULT_DATA_OUT_FILE, "wb");
                }
                else {
                    fout = fopen(buffer, "ab");
                }
                if(!fout) {
                    daq_log(LOG_ERROR, "Could not open file '%s': %s", buffer, strerror(errno));
                    daq_log(LOG_ERROR, "Events will not be saved!");
                }
                daq_log(LOG_WARN, "Writing data to new file %s\n", buffer);

                bytes_in_file = 0;
                start_new_run = 0;
            }
        }

        // Reset the publish data-rate limit every 10th of a second.
        // Do it every 10th of a second otherwise the publish'd data will look
        // very stuttery if it's reset every second.
        delta_t = calculate_delta_t(current_time, byte_sent_time);
        if(delta_t > 100000) {
            print_status_bytes_sent += bytes_sent;
            bytes_sent = 0;
            byte_sent_time = current_time;
        }

        // Print heartbeat if enough time has passed
        delta_t = calculate_delta_t(current_time, event_rate_time);
        if(delta_t > PRINT_UPDATE_COOLDOWN) {
            float redis_bytes = 1e6*print_status_bytes_sent/(delta_t*1024.);
            char prefix = 'k';
            if(redis_bytes > 1024) {
                redis_bytes /= 1024.;
                prefix = 'M';
            }

            daq_log(LOG_INFO, "Event id %i.\t%0.2f events per second.\t%0.1f%cB/s to redis.", event_id, (float)1e6*built_count/PRINT_UPDATE_COOLDOWN, redis_bytes, prefix);
            built_count = 0;
            print_status_bytes_sent = 0;
            event_rate_time = current_time;
        }

        stats.uptime = (current_time.tv_sec*1e6 + current_time.tv_usec) - stats.start_time;
        if((stats.uptime - last_status_update_time) > REDIS_STATS_COOLDOWN) {
            stats.events_waiting = event_ready_queue.events_available;
            stats.run_number = run_info.run_number;
            stats.sub_run_number = run_info.sub_run;

            redis_publish_stats(publish_redis, &stats);

            stats.max_delta_t = 0;
            stats.fontus_delta_t = 0;
            last_status_update_time = stats.uptime;
        }
    }
    // Clean up
    fclose(fout);
    redisFree(data_redis);
    redisFree(publish_redis);
    redisFree(run_info_redis);
    daq_log(LOG_WARN, "Bye\n");
    return 0;
}
