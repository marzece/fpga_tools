/*
Author: Eric Marzec <marzece@gmail.com>

   This program handles getting data from the FPGA and reading it into a
   buffer, then processing the data by finding the start/end of each event.
   Also checks the various CRC's to ensure the data is good.

   The primary data structure for storing data while processing it is a ring
   buffer. There's one slightly non-stanard element to the ring buffer as
   implemented here, I use two different "read_pointers", one read_pointer only
   updates by moving from event start to event start, and it should never point
   to data thats in the middle of an event. The second read pointer can point
   anywhere and is used for looking at data while you search for an events end.
   Once the end of an event is found, the event-by-event read pointer is moved up.
   Data can only be written into the ring buffer upto the event-by-event read pointer.

   Event data is handled by just remembering the location of the start in the memory
   buffer, and the event length. B/c an event can wrap around the end of the
   memory buffer an event can end up being split, in which case the location of
   the start is recorded, and the location of the second "start". In principle
   that second start will always be at memory buffer location 0.

   Once a full event is recorded, the data for it is dispatched to a redis
   pub-sub stream and also written to disk.

   In the event that something bad happens and a new event's header is wrong, or
   a new event isn't found immediatly after the end of the previous event the program
   is set into "reeling" mode. In that mode the program just scans through values looking
   for the start of a new event (demarcated by the 32-bit word 0xFFFFFFFF). Once that is
   found the program exits "reeling" mode and resumes normal data processing.
 */

// TODO
// Redo the "reeling" functionality
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/uio.h>
#include "hiredis/hiredis.h"
#include "fnet_client.h"
#include "daq_logger.h"

#include "data_builder.h"

#if __linux__
#define htonll(x) ((((uint64_t)htonl(x)) << 32) + htonl((x) >> 32))
#define ntohll(x) htonll(x)
#endif

// QUICKACK doesn't seem to improve readout speed. So I've disabled it for now
#define DO_QUICKACK 0

// Largest possible size for single message (in bytes)
#define LOG_MESSAGE_MAX 1024

#define DEFAULT_REDIS_HOST  "127.0.0.1"
#define DEFAULT_ERROR_LOG_FILENAME "data_builder_error_log.log"

// Every event header starts with this magic number
#define CERES_MAGIC_VALUE  0xFFFFFFFFUL
#define FONTUS_MAGIC_VALUE 0xF00FF00FUL
#define CERES_HEADER_SIZE 20
#define FONTUS_HEADER_SIZE 52

// Space that should be allocated for data buffers
#define BUFFER_SIZE (10*1024*1024) // 10 MB
#define EVENT_BUFFER_SIZE BUFFER_SIZE

// Number of channels to be readout
// Note: at one point the design would have a varying number of channels per CERES
// board. Nowadays it's pretty much always gonna be 16
#define NUM_CHANNELS 16

#ifdef DUMP_DATA
FILE* fdump = NULL;
#endif

int builder_verbosity_stdout = LOG_INFO;
int builder_verbosity_redis = LOG_WARN;
int builder_verbosity_file = LOG_WARN;


// Counter for how many events have been built
int built_counter = 0;

uint32_t crc32(uint32_t crc, uint32_t * buf, unsigned int len);
void crc8(unsigned char *crc, unsigned char m);

// The two CERES XEMs both readout 16-channels, but the data ends up in weird
// locations.  The two arrays below re-map the channels such that the top-most
// channels (physically located) ends up being the earliest in the serial data
// stream
static const int channel_order_xem1[NUM_CHANNELS]={3,2,1,0,7,6,5,4,11,10,9,8, 15,14,13,12};
static const int channel_order_xem2[NUM_CHANNELS]={15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0};

// FILE handle for writing to disk
static FILE* fdisk = NULL;
// Log file

// Redis connection for data
redisContext* redis = NULL;

// Variable for deciding to stay in the main loop or not.
// When loop is zero program should exit soon after.
// Can be set inside a signal handler, thus the weird type
volatile sig_atomic_t loop = 1;

// If reeling==1 need to search for next trigger header magic value.
int reeling = 0;
int timestamps_enabled = 0;

// Alias the daq_logger log function to builder_log just b/c I like that name more
void(*builder_log)(int, const char* restrict, ...) = &daq_log;

typedef struct RingBuffer {
    unsigned char* buffer;
    size_t event_read_pointer;
    size_t read_pointer;
    size_t write_pointer;
    int is_empty;
} RingBuffer;

//  Just a big long contiguous chunk of data for holding waveforms
typedef struct EventBuffer {
    unsigned char* data;
    size_t num_bytes;
} EventBuffer;

typedef struct ProcessingStats {
    unsigned int event_count; // Number of events built (since program startup)
    unsigned int trigger_id; // Most recent event's trigger_id
    int device_id; // Most recent event's device ID (shouldn't change event-by-event)
    unsigned long long latest_timestamp; // Most recent event's clock timestamp
    int reeling_happened; // Tracks if the data builder was in  the reeling state any time since the previous update.
    double start_time; // In microseconds (since Epoch start)
    double uptime; // In microseconds
    unsigned int pid; // PID for this program
    int connected_to_fpga;
    int fifo_rpointer;
    int fifo_event_rpointer;
    int fifo_wpointer;
    // Would like to keep track of running compression factor?

    // Would like to add these but its a bit of a pain
    //unsigned int bytes_read;
    //unsigned int bytes_written;
} ProcessingStats;

// TODO could consider merging the contiguous & total space available functions
// by have both values calculated and returned in argument pointers..and just only fill in
// the non-NULL ones.
size_t ring_buffer_contiguous_space_available(RingBuffer* ring_buffer) {
    if(ring_buffer->is_empty) {
        ring_buffer->event_read_pointer = 0;
        ring_buffer->read_pointer = 0;
        ring_buffer->write_pointer = 0;
        return BUFFER_SIZE;
    }
    if(ring_buffer->write_pointer == ring_buffer->event_read_pointer) {
        return 0;
    }
    if(ring_buffer->write_pointer < ring_buffer->event_read_pointer) {
        return ring_buffer->event_read_pointer - ring_buffer->write_pointer;
    }
    return BUFFER_SIZE - ring_buffer->write_pointer;
}

size_t ring_buffer_space_available(RingBuffer* ring_buffer) {
    if(ring_buffer->is_empty) {
        ring_buffer->event_read_pointer = 0;
        ring_buffer->read_pointer = 0;
        ring_buffer->write_pointer = 0;
        return BUFFER_SIZE;
    }
    if(ring_buffer->write_pointer == ring_buffer->event_read_pointer) {
        return 0;
    }
    if(ring_buffer->write_pointer < ring_buffer->event_read_pointer) {
        // If rpointer is "ahead" of the wpointer then it must
        // mean the write pointer has wrapped around to the start of
        // the buffer and the read pointer hasn't (yet).
        return ring_buffer->write_pointer + (BUFFER_SIZE - ring_buffer->event_read_pointer);
    }
    return ring_buffer->write_pointer - ring_buffer->event_read_pointer;
}

size_t ring_buffer_contiguous_readable(RingBuffer* buffer) {
    /* This is a little bit tricky b/c of the two read pointers...but it's not too bad.
     * Basically there are two cases where are all three pointers are equal, full or empty,
     * those cases are disambiguated by the "is_empty" flag.
     * If only the "read_pointer" and the write_pointer are equal that will only happen if we've
     * read all available data (no more is readable).
     * Every other case can ignore the event_read_pointer and just do reading like a normal
     * ring buffer.
     */
    if(buffer->is_empty) {
        return 0;
    }
    if(buffer->read_pointer == buffer->write_pointer && buffer->event_read_pointer == buffer->write_pointer) {
        return BUFFER_SIZE - buffer->read_pointer;
    }

    if(buffer->read_pointer == buffer->write_pointer) {
        return 0;
    }
    // Okay now that we're here can ignore the event_read_pointer and just act like this
    // is a normal ring buffer
    if(buffer->write_pointer < buffer->read_pointer) {
        return BUFFER_SIZE - buffer->read_pointer;
    }
    return buffer->write_pointer - buffer->read_pointer;

}

size_t ring_buffer_readable(RingBuffer* buffer) {
    // See comment in ring_buffer_contiguous_readable for why this is a bit complicated
    if(buffer->is_empty) {
        return 0;
    }

    if(buffer->read_pointer == buffer->write_pointer && buffer->event_read_pointer == buffer->write_pointer) {
        // Buffer is full
        return BUFFER_SIZE;
    }
    if(buffer->read_pointer == buffer->write_pointer) {
        return 0;
    }

    // Now can treat this like a normal ring_buffer and ignore the event_read_pointer
    if(buffer->read_pointer > buffer->write_pointer) {
        // Readable data  wraps around the end of buffer, then upto the write_pointer
        return buffer->write_pointer  + (BUFFER_SIZE - buffer->read_pointer);
    }
    return buffer->write_pointer - buffer->read_pointer;
}

void ring_buffer_update_write_pntr(RingBuffer* buffer, size_t nbytes) {
    // Handle the trivial case
    if(nbytes == 0) {
        return;
    }

    // First if we're writing any data the buffer cannot be emtpy
    if(nbytes <= ring_buffer_contiguous_space_available(buffer)) {
        buffer->write_pointer += nbytes;
    }
    else if(nbytes <= ring_buffer_space_available(buffer)) {
        // If here it means the write pointer is gonna go over the "end" of
        // the buffer and we need to wrap around to the start
        buffer->write_pointer = nbytes - (BUFFER_SIZE - buffer->write_pointer);
    }
    else {
        // If here it's a pretty serious error. It means the buffer is full and
        // some data in the "read" chunk of the buffer was probably overwritten.
        // I'm not sure how this hould be handled, right now I'll just emit a message.
        // Perhaps I should just flush the buffer and set go into "reeling" mode
        builder_log(LOG_ERROR, "DATA was overwritten probably!!!!\n"
                "This error is not handled so you should probably just restart things"
                "...and figure out how this happened");
    }

    // If we wrote a non-zero number of bytes, the buffer is not empty
    buffer->is_empty = 0;

    // Do the wrap if need be
    if(buffer->write_pointer == BUFFER_SIZE) {
        buffer->write_pointer = 0;
    }
}

void ring_buffer_update_event_read_pntr(RingBuffer* buffer) {
    // This function assumes the read_pointer is at an event boundary,
    // should perhaps not rely on that assumption and just let the caller
    // tell me where to set the event_read_pointer

    buffer->event_read_pointer = buffer->read_pointer;
    // If the read pointer is now caught up with the write pointer,
    // make sure to update the "is_empty" state var, and might as well
    // move everything back to the start of the buffer to maximize contiguous
    // space available.
    if(buffer->event_read_pointer == buffer->write_pointer) {
        buffer->read_pointer = 0;
        buffer->event_read_pointer = 0;
        buffer->write_pointer = 0;
        buffer->is_empty = 1;
    }
}

void ring_buffer_update_read_pntr(RingBuffer* buffer, size_t nbytes) {
    // This only does the read_pointer, not the event_read_pointer
    // Will not update is_empty either.

    // First handle the trival case
    if(nbytes == 0) {
        return;
    }

    // Fist, do the simple read if possible
    if(nbytes <= ring_buffer_contiguous_readable(buffer)) {
        buffer->read_pointer += nbytes;
    }
    else if(nbytes <= ring_buffer_readable(buffer)) {
        // This is the wrap around read
        buffer->read_pointer = nbytes - (BUFFER_SIZE - buffer->read_pointer);
    }
    else {
        // DATA was read too far, this shouldn't happen ever
        builder_log(LOG_ERROR, "Invalid data was read!!!\n"
                "This really should not have happened."
                " Everything will probably be wrong from here on out");
    }

    // Handle the exact wrap case
    if(buffer->read_pointer == BUFFER_SIZE) {
        buffer->read_pointer = 0;
    }
}

typedef struct FPGA_IF {
    int fd; // File descriptor for tcp connection
    struct fnet_ctrl_client* udp_client; // UDP connection
    RingBuffer ring_buffer; // compressed data buffer
    EventBuffer event_buffer; // memory location for uncompressed event data
} FPGA_IF;

typedef struct CeresTrigHeader{
    uint32_t magic_number;
    uint32_t trig_number;
    uint64_t clock;
    uint16_t length;
    uint8_t device_number;
    uint8_t crc;
} CeresTrigHeader;

typedef struct ChannelHeader {
    uint8_t reserved1;
    uint8_t channel_id1;
    uint8_t reserved2;
    uint8_t channel_id2;
} ChannelHeader;

typedef struct FontusTrigHeader{
    uint32_t magic_number;
    uint32_t trig_number;
    uint64_t clock;
    uint16_t length;
    uint8_t device_number;
    uint8_t trigger_flags;
    uint32_t self_trigger_word;
    uint64_t beam_trigger_time;
    uint64_t led_trigger_time;
    uint64_t ct_time;
    uint32_t crc;
} FontusTrigHeader;

typedef union EventHeader {
    FontusTrigHeader fontus;
    CeresTrigHeader ceres;
}EventHeader;

struct BuilderProtocol {
    int(*reader_process)(FPGA_IF* fpga, EventHeader *ret);
    void (*display_process)(const EventHeader* header);
    void (*write_event)(EventBuffer* eb, EventHeader* header);
    int (*validate_event)(const EventHeader* header, const EventBuffer* eb);
    void (*publish_event)(redisContext*c, EventBuffer eb, const unsigned int header_size);
    void (*update_stats)(ProcessingStats* stats, EventHeader* header);

};

// This handles keeping track of reading an event while in the middle of it
typedef struct EventInProgress {
    EventHeader event_header;
    int header_bytes_read;
    int data_bytes_read;
    int current_channel;
    int wf_header_read;
    int samples_read;
    int wf_crc_read;
    uint16_t prev_sample;
    uint16_t max;
    uint16_t min;
} EventInProgress;

// Open socket to FPGA returns 0 if successful
int connect_to_fpga(const char* fpga_ip) {
    const int port = 5009; // FPGA doesn't use ports, so this doesn't matter
    int args;
    int res;
    struct sockaddr_in fpga_addr;
    fpga_addr.sin_family = AF_INET;
    fpga_addr.sin_addr.s_addr = inet_addr(fpga_ip);
    fpga_addr.sin_port = htons(port);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        builder_log(LOG_ERROR, "Error creating TCP socket: %s", strerror(errno));
        return fd;
    }

    // Set the socket to non-block before connecting
    args  = fcntl(fd, F_GETFL, NULL);
    if(args < 0) {
        builder_log(LOG_ERROR, "Error getting socket opts");
        goto error;

    }
    args |= O_NONBLOCK;

    if(fcntl(fd, F_SETFL, args) < 0) {
        builder_log(LOG_ERROR, "Error setting socket opts");
        goto error;
    }
    fd_set myset;

    while(1) {
        res = connect(fd, (struct sockaddr*)&fpga_addr, sizeof(fpga_addr));
        if(res < 0) {
            if(errno == EISCONN) {
                break;
            }
            else if (errno == EINPROGRESS) {
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 500000;
                FD_ZERO(&myset);
                FD_SET(fd, &myset);
                res = 0;
                res = select(fd+1, NULL, &myset, NULL, &tv);
                if(res != 1) {
                    goto error;
                }
                int so_error;
                socklen_t len = sizeof(so_error);
                res = getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if(res < 0) {
                    builder_log(LOG_ERROR, "getsockopt failed: %s", strerror(errno));
                    goto error;

                }
                if(so_error !=0) {
                    builder_log(LOG_ERROR, "Connection failed: %s", strerror(so_error));
                    goto error;

                }
                break;
            }
            builder_log(LOG_ERROR, "Error connecting TCP socket: %s", strerror(errno));
            continue;
        }
        break;
    }

    // Set it back to blocking mode
    // Idk if it's necessary to get args again, but that's how it's done on stack overflow
    //args  = fcntl(fd, F_GETFL, NULL);
    //if(args < 0) {
    //    printf("Error getting socket opts2\n");
    //    goto error;

    //}
    //args &= (~O_NONBLOCK);

    if(fcntl(fd, F_SETFL, args) < 0) {
        builder_log(LOG_ERROR, "Error setting socket opts2");
        goto error;

    }

    // Set QUICK ACK & TCP_NODELAY
    // NO_DELAY does nothing I think, it only matters for sending data not
    // receiving (pretty sure)
    int yes = 1;
#if defined(__unix__) && DO_QUICKACK
    if( setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &yes, sizeof(yes)) ) {
        builder_log(LOG_ERROR, "Error setting TCP_QUICKACK");
    }
#endif

    }

    timestamps_enabled = 1;
    if(setsockopt(fd, SOL_SOCKET, SO_TIMESTAMP, &yes, sizeof(yes)) ) {
        builder_log(LOG_ERROR, "Error setting SO_TIMESTAMP");
        timestamps_enabled = 0;
    }
    return fd;

error:
    close(fd);
    // reuse adress
    return -1;
}

// This is the function that reads data from the FPGA ethernet connection
unsigned char control_buf[1024];
size_t pull_from_fpga(FPGA_IF* fpga_if) {
    ssize_t bytes_recvd = 0;
    size_t contiguous_space_left;

    unsigned char* w_buffer = fpga_if->ring_buffer.buffer;
    size_t w_buffer_idx = fpga_if->ring_buffer.write_pointer;

    // TODO could consider checking total_space, not just contiguous space and doing two
    // recv's, one at the "end" then one at the start of the ring buffer.
    // That might help if this gets a lot of chump reads that are only like 100 bytes.
    contiguous_space_left = ring_buffer_contiguous_space_available(&(fpga_if->ring_buffer));

    struct iovec buf;
    buf.iov_base = w_buffer+w_buffer_idx;
    buf.iov_len = contiguous_space_left;
    struct msghdr message_header;
    message_header.msg_name = NULL;
    message_header.msg_namelen = 0;
    message_header.msg_iov = &buf;
    message_header.msg_iovlen = 1;
    message_header.msg_control = control_buf;
    message_header.msg_controllen = 1024;

    if(contiguous_space_left > 0) {
        //bytes_recvd = recv(fpga_if->fd, w_buffer + w_buffer_idx, contiguous_space_left, 0);
        bytes_recvd = recvmsg(fpga_if->fd, &message_header, 0);
        if(bytes_recvd < 0) {
            //printf("Error retrieving data from socket: %s\n", strerror(errno));
            return 0;
        }
#ifdef DUMP_DATA
        fwrite(w_buffer + w_buffer_idx, 1, bytes_recvd, fdump);
#endif
        ring_buffer_update_write_pntr(&fpga_if->ring_buffer, bytes_recvd);
    }
    return bytes_recvd;
}

void initialize_ring_buffer(RingBuffer* ring_buffer) {
    ring_buffer->read_pointer = 0;
    ring_buffer->event_read_pointer = 0;
    ring_buffer->write_pointer = 0;
    ring_buffer->is_empty = 1;
    ring_buffer->buffer = malloc(BUFFER_SIZE);
    if(!ring_buffer->buffer) {
        builder_log(LOG_ERROR, "Could not allocate enough space for data buffer!");
        exit(1);
    }
}

void initialize_event_buffer(EventBuffer* eb) {
    eb->num_bytes = 0;
    eb->data = malloc(EVENT_BUFFER_SIZE);
    if(!eb->data) {
        builder_log(LOG_ERROR, "Could not allocate enough space for event buffer!");
        exit(1);
    }
}

// Connect to redis database
redisContext* create_redis_conn(const char* hostname, const int port) {
    builder_log(LOG_INFO, "Opening Redis Connection: %s", hostname);

    redisContext* c;
    c = redisConnect(hostname, port);
    if(c == NULL || c->err) {
        builder_log(LOG_ERROR, "Redis connection error %s", (c ? c->errstr : ""));
        redisFree(c);
        return NULL;
    }
    return c;
}

redisContext* create_redis_unix_conn(const char* path) {
    printf("Opening Redis Connection\n");
    redisContext* c;
    c = redisConnectUnix(path);
    if(c == NULL || c->err) {
        printf("Redis connection error %s\n", c->errstr);
        redisFree(c);
        return NULL;
    }
    return c;
}

EventInProgress start_event(void) {
    EventInProgress ev;

    ev.header_bytes_read = 0;
    ev.data_bytes_read = 0;
    ev.current_channel = 0;
    ev.wf_header_read = 0;
    ev.samples_read = 0;
    ev.wf_crc_read = 0;
    ev.prev_sample = 0;

    return ev;
}

uint8_t crc_from_bytes(unsigned char* bytes, int length, unsigned char init) {
    static int is_swapped;
    static int first = 1;
    // Run time test of byte order, should only ever happen once.
    if(first) {
        first = 0;
        is_swapped = htonl(1) != 1;
    }

    int i;
    unsigned char crc = init;
    if(is_swapped) {
        for(i=length-1; i >= 0; i--) {
            crc8(&crc, bytes[i]);
        }
    }
    else {
        for(i = 0; i < length; i++) {
            crc8(&crc, bytes[i]);
        }
    }
    return crc;
}

void clean_up(void) {
    builder_log(LOG_INFO, "Closing and cleaning up");
    if(fdisk) {
        builder_log(LOG_INFO, "Closing data file");
        fclose(fdisk);
    }
    cleanup_logger();
    redisFree(redis);
    redis = NULL;
}

void end_loop(void) {
    loop = 0;
}

static void sig_handler(int signum) {
    static int num_kills = 0;
    // TODO, 'builder_log' should not be called in a signal handler, but I do
    // like the builder logging that it recieved a singal. So I should have the
    // logging done outside this handler.
    builder_log(LOG_WARN, "Sig recieved %i", signum);
    if(signum == SIGINT || signum == SIGTERM) {
        num_kills +=1;
        end_loop();
    }
    if(num_kills >= 2) {
        exit(1);
    }
}

void ceres_write_to_disk(EventBuffer* eb, EventHeader* header) {
    (void)header; // Unused
    size_t nwritten;
    nwritten = fwrite(eb->data, 1, eb->num_bytes, fdisk);
    if(nwritten != eb->num_bytes) {
        // TODO check errno
        builder_log(LOG_ERROR, "Error writing event");
        // TODO close the file??
        return;
    }
    fflush(fdisk);
}

void fontus_write_to_disk(EventBuffer* eb, EventHeader* header) {
    (void)eb; // Unused
    FontusTrigHeader* ev = (FontusTrigHeader*)header;
    size_t nwritten;
    unsigned int byte_count = 0;
    // Write header
    {
        unsigned char header_mem[FONTUS_HEADER_SIZE];
        *((uint32_t*)header_mem) = htonl(ev->magic_number);
        byte_count += 4;
        *((uint32_t*)(header_mem + byte_count)) = htonl(ev->trig_number);
        byte_count += 4;
        *((uint64_t*)(header_mem + byte_count)) = htonll(ev->clock);
        byte_count += 8;
        *((uint16_t*)(header_mem + byte_count)) = htons(ev->length);
        byte_count += 2;
        *((uint8_t*)(header_mem + byte_count)) = ev->device_number;
        byte_count += 1;
        *((uint8_t*)(header_mem + byte_count)) = ev->trigger_flags;
        byte_count += 1;
        *((uint32_t*)(header_mem + byte_count)) = htonl(ev->self_trigger_word);
        byte_count += 4;
        *((uint64_t*)(header_mem + byte_count)) = htonll(ev->beam_trigger_time);
        byte_count += 8;
        *((uint64_t*)(header_mem + byte_count)) = htonll(ev->led_trigger_time);
        byte_count += 8;
        *((uint64_t*)(header_mem + byte_count)) = htonll(ev->ct_time);
        byte_count += 8;
        *((uint32_t*)(header_mem + byte_count)) = htonl(ev->crc);
        byte_count += 4;

        nwritten = fwrite(header_mem, 1, byte_count, fdisk);
    }

    if(nwritten != byte_count) {
        // TODO check errno (does fwrite set errno?)
        builder_log(LOG_ERROR, "Error writing event header!");
        // TODO do I want to close the file here?
        return;
    }

    fflush(fdisk);
}

// Read 32 bits from read buffer
int pop32(RingBuffer* buffer, uint32_t* val) {
    size_t i;
    // Make sure a NULL pntr wasn't passed in
    if(!val) {
        return -1;
    }
    size_t contiguous_space = ring_buffer_contiguous_readable(buffer);
    size_t total_space = ring_buffer_readable(buffer);
    if(total_space < 4) {
        // Not enough left to read
        return -1;
    }
    if(contiguous_space >= 4) {
        *val = ntohl(*(uint32_t*)(buffer->buffer+buffer->read_pointer));
    }
    else if(total_space >= 4) {
        // Need to read around the wrap (sigh)
        // Have to do this one byte at a time to ensure I don't read off the edge
        *val = 0;
        for(i=0; i< contiguous_space; i++) {
            *(unsigned char*)val |= *(buffer->buffer + buffer->read_pointer + i);
            *val = *val << 8;
        }
        for(i=0; i< 4-contiguous_space; i++) {
            *(unsigned char*)val |= *(buffer->buffer + i);
            *val = *val << 8;
        }

        *val = ntohl(*val);
    }

    ring_buffer_update_read_pntr(buffer, 4);
    return 0;
}

void stash_with_reorder(EventBuffer* eb, uint32_t word, int current_channel, int isample, int wf_length, int is_xem1_not_xem2) {
    // Reordering for CRC, Uncompressed Data
    // In this function, if reorder is needed change buffer_locatioin and write data in buffer,
    // After that going back to original buffer_location before reordering.
    //
    const int* channel_order = is_xem1_not_xem2 ? channel_order_xem1 : channel_order_xem2;
    int new_channel = channel_order[current_channel];

    // The first sample is always 0xXXFFXXFF where XX is the channel number,
    // since we're re-ordering this we'll change the XX to the new channel
    if(isample == 0) {
        word = 0xFF00FF00 | new_channel | (new_channel<<16);
    }

    // Calculate where this sample should go, in bytes from the start
    // The below +2 is b/c the channel header & channel footer are both 32-bit words
    // Also each "sample" (really pair of samples) is 32-bits
    int offset = CERES_HEADER_SIZE + new_channel*((wf_length+2)*4) + isample*4;
    *(uint32_t*)(eb->data + offset)= htonl(word);
    eb->num_bytes += 4;
}

int find_event_start(FPGA_IF* fpga, const uint32_t event_start_word) {
    /* This just searches through the readable memory buffer and tries to find the
       magic values (0xFFFFFFFF) that indicates the start of a header
       If found this function returns 1 otherwise returns 0.

       This function will fail if the bytes for the magic_value are split across the
       ring_buffer wrap...TODO could try and this to fix that.
    */
    size_t i;
    int found = 0;
    size_t contiguous_space = ring_buffer_contiguous_readable(&(fpga->ring_buffer));
    size_t total_space = ring_buffer_readable(&(fpga->ring_buffer));

    // This is the case where there's not enough data left in the buffer till the end to contain
    // the MAGIC_VALUE,
    // nor is there is enough data in the buffer as a whole. In which case we should just leave now
    // and come back when there's more data to process
    if(contiguous_space < 4 && (total_space-contiguous_space) < 4) {
        return 0;
    }

    if(contiguous_space < 4) {
        // If there's not enough contiguous data, but there is enough data in the other contiguous
        // chunk, we should just skip to the start of the next contiguous chunk
        ring_buffer_update_read_pntr(&fpga->ring_buffer, contiguous_space);
        contiguous_space = total_space - contiguous_space;
        total_space = contiguous_space;
    }


    for(i=0; i < contiguous_space-3; i++) {
        unsigned char* current = fpga->ring_buffer.buffer + fpga->ring_buffer.read_pointer;
        if(*(current+0) == (event_start_word & 0xFF) &&
           *(current+1) == ((event_start_word & 0xFF00)>>8) &&
           *(current+2) == ((event_start_word & 0xFF0000)>>16) &&
           *(current+3) == ((event_start_word & 0xFF000000)>>24)) {
            found = 1;
            break;
        }
        ring_buffer_update_read_pntr(&fpga->ring_buffer, 1);
    }

    if(!found) {
        // The only way you should get here is if you searched to the end of the buffer
        // and didn't find an event start... Need to push the read_pointer to the end of
        // the buffer
        ring_buffer_update_read_pntr(&fpga->ring_buffer, 3);
    }

    // Regardless of if you're here b/c the event was found or not, update the
    // event_read_pointer b/c all the data upto the current read_pointer is useless and
    // can be trashed.
    ring_buffer_update_event_read_pntr(&(fpga->ring_buffer));

    return found;
}

void fontus_publish_event(redisContext*c, const FontusTrigHeader event) {
    if(!c) {
        return;
    }
    char publish_buffer[FONTUS_HEADER_SIZE];
    unsigned int offset = 0;

    redisReply* r;
    size_t arglens[3];
    const char* args[3];

    args[0] = "PUBLISH";
    arglens[0] = strlen(args[0]);
    args[1] = "event_stream";
    arglens[1] = strlen(args[1]);

    *(uint32_t*)(publish_buffer+offset) = htonl(event.magic_number);
    offset += 4;
    *(uint32_t*)(publish_buffer+offset) = htonl(event.trig_number);
    offset += 4;
    *(uint64_t*)(publish_buffer+offset) = htonll(event.clock);
    offset += 8;
    *(uint16_t*)(publish_buffer+offset) = htons(event.length);
    offset += 2;
    *(uint8_t*)(publish_buffer+offset) = event.device_number;
    offset += 1;
    *(uint8_t*)(publish_buffer+offset) = event.trigger_flags;
    offset += 1;
    *(uint32_t*)(publish_buffer+offset) = htonl(event.self_trigger_word);
    offset += 4;
    *(uint64_t*)(publish_buffer+offset) = htonll(event.beam_trigger_time);
    offset += 8;
    *(uint64_t*)(publish_buffer+offset) = htonll(event.led_trigger_time);
    offset += 8;
    *(uint64_t*)(publish_buffer+offset) = htonll(event.ct_time);
    offset += 8;
    *(uint32_t*)(publish_buffer+offset) = htonl(event.crc);
    offset += 4;

    arglens[2] = FONTUS_HEADER_SIZE;
    args[2] = publish_buffer;
    r = redisCommandArgv(c, 3,  args,  arglens);
    if(!r) {
        builder_log(LOG_ERROR, "Redis error!");
    }
    freeReplyObject(r);

    args[1] = "header_stream";
    arglens[1] = strlen(args[1]);
    r = redisCommandArgv(c, 3,  args,  arglens);
    if(!r) {
        builder_log(LOG_ERROR, "Redis error!");
    }
    freeReplyObject(r);
}

// Send event to redis database
void publish_event(redisContext*c, EventBuffer eb, const unsigned int header_size) {
    if(!c) {
        return;
    }
    redisReply* r;
    size_t arglens[3];
    const char* args[3];

    args[0] = "PUBLISH";
    arglens[0] = strlen(args[0]);
    args[1] = "event_stream";
    arglens[1] = strlen(args[1]);
    args[2] = (char*)eb.data;
    arglens[2] = eb.num_bytes;

    r = redisCommandArgv(c, 3,  args,  arglens);
    if(!r) {
        builder_log(LOG_ERROR, "Redis error: %s", c->errstr);
    }
    freeReplyObject(r);

    // Also publish the header in a seperate stream
    args[1] = "header_stream";
    arglens[1] = strlen(args[1]);

    // args[2] already contains the whole event, including the header.
    // So just send the first HEADER_SIZE of the event
    arglens[2] = header_size;
    r = redisCommandArgv(c, 3,  args,  arglens);
    if(!r) {
        builder_log(LOG_ERROR, "Redis error! : %s", c->errstr);
    }
    freeReplyObject(r);
}

void redis_publish_stats(redisContext* c, const ProcessingStats* stats) {
    if(!c || !stats) {
        return;
    }

    redisReply* r;
    size_t arglens[3];
    const char* args[3];

    char buf[2048];

    // Use a a Stream instead of a pub-sub for this?
    // Or maybe just a key-value store?
    args[0] = "PUBLISH";
    arglens[0] = strlen(args[0]);

    args[1] = "builder_stats";
    arglens[1] = strlen(args[1]);

    arglens[2] = snprintf(buf, 2048, "%i %u %llu %i %i %i %i %i %i %i", stats->event_count,
                                                          stats->trigger_id,
                                                          stats->latest_timestamp,
                                                          stats->device_id,
                                                          stats->reeling_happened,
                                                          stats->fifo_event_rpointer,
                                                          stats->fifo_rpointer,
                                                          stats->fifo_wpointer,
                                                          stats->pid,
                                                          (int)(stats->uptime/1e6));
    args[2] = buf;
    r = redisCommandArgv(c, 3,  args,  arglens);
    if(!r) {
        builder_log(LOG_ERROR, "Error sending stats update to redis");
    }
    freeReplyObject(r);
}

struct fnet_ctrl_client* connect_fakernet_udp_client(const char* fnet_hname) {
    int reliable = 0; // wtf does this do?
    const char* err_string = NULL;
    struct fnet_ctrl_client* fnet_client = NULL;

    fnet_client = fnet_ctrl_connect(fnet_hname, reliable, &err_string, NULL);
    if(!fnet_client) {
        builder_log(LOG_ERROR, "ERROR Connecting on UDP channel: %s. Will retry", err_string);
        return NULL;
    }
    builder_log(LOG_INFO, "UDP channel connected");
    return fnet_client;
}

int send_tcp_reset(struct fnet_ctrl_client* client) {
    if(!client) {
        builder_log(LOG_ERROR, "Cannot send TCP reset: UDP client not established");
        return -1;
    }
    int ret;
    fakernet_reg_acc_item* send_buf;
    fnet_ctrl_get_send_recv_bufs(client, &send_buf, NULL);

    send_buf[0].addr = htonl(FAKERNET_REG_ACCESS_ADDR_WRITE | FAKERNET_REG_ACCESS_ADDR_RESET_TCP);
    send_buf[0].data = htonl(0);
    ret = fnet_ctrl_send_recv_regacc(client, 1);
    if(ret == 0) {
        builder_log(LOG_ERROR, "Error happened while doing TCP-Reset");
        return -1;
    }
    return 0;
}

void initialize_stats(ProcessingStats* stats) {
    struct timeval tv;
    stats->event_count = 0;
    stats->trigger_id = 0;
    stats->device_id = -1;
    stats->latest_timestamp = 0;
    stats->pid = (unsigned int)getpid();
    stats->uptime = 0;
    stats->connected_to_fpga = 0;
    stats->fifo_event_rpointer = 0;
    stats->fifo_rpointer = 0;
    stats->fifo_wpointer = 0;
    //stats->bytes_read = 0;
    //stats->bytes_written = 0;

    gettimeofday(&tv, NULL);
    stats->start_time = tv.tv_sec*1e6 + tv.tv_usec;
}

uint32_t calc_fontus_header_crc(FontusTrigHeader* header) {
    // Need to layout the Header in a contiguous array so I can run the CRC
    // calculation on it. The struct is probably contigous, but better safe
    // than sorry.
    // The buffer doesn't need space for the magic_number, or the CRC though.
    char buffer[FONTUS_HEADER_SIZE-8];
    *(uint32_t*)(buffer+0) = ntohl(header->trig_number);
    *(uint64_t*)(buffer+4) = ntohll(header->clock);
    *(uint16_t*)(buffer+12) = ntohs(header->length);
    *(uint8_t*)(buffer+14) = header->device_number;
    *(uint8_t*)(buffer+15) = header->trigger_flags;
    *(uint32_t*)(buffer+16) = ntohl(header->self_trigger_word);
    *(uint64_t*)(buffer+20) = ntohll(header->beam_trigger_time);
    *(uint64_t*)(buffer+28) = ntohll(header->led_trigger_time);
    *(uint64_t*)(buffer+36) = ntohll(header->ct_time);
    return crc32(0, (uint32_t*)buffer, 44);
}

uint8_t calc_ceres_header_crc(CeresTrigHeader* header) {
    unsigned char crc = 0;
    crc = crc_from_bytes((unsigned char*)&header->trig_number, 4, crc);
    crc = crc_from_bytes((unsigned char*)&header->clock, 8, crc);
    crc = crc_from_bytes((unsigned char*)&header->length, 2, crc);
    crc8(&crc, header->device_number);
    return crc ^ 0x55; // The 0x55 here makes it the ITU CRC8 implemenation
}

void calculate_waveform_crcs(const CeresTrigHeader* header, const EventBuffer* event, uint32_t* calculated_crcs, uint32_t* given_crcs) {
    int i;
    int length = header->length;
    uint32_t* wf_start = (uint32_t*)(event->data + CERES_HEADER_SIZE + 4);
    for(i=0; i<NUM_CHANNELS; i++) {
        uint32_t this_crc = crc32(0, wf_start, length*sizeof(uint32_t));
        //uint32_t found_crc =  *(uint32_t*)(wf_start + length);
        // TODO I don't understand why this need's to be ntohl'd ???
        uint32_t found_crc =  ntohl(*(uint32_t*)(wf_start + length));
        calculated_crcs[i] = this_crc;
        given_crcs[i] = found_crc;
        wf_start += length + 2;
    }
}

void fontus_interpret_header_word(FontusTrigHeader* header, const uint32_t word, const int which_word) {
        switch(which_word) {
            case 0:
                header->magic_number = word;
                break;
            case 1:
                header->trig_number = word;
                break;
            case 2:
                header->clock &= 0x00000000FFFFFFFFll;
                header->clock |= (uint64_t)((uint64_t)word << 32);
                break;
            case 3:
                header->clock &= 0xFFFFFFFF00000000ll;
                header->clock |= word;
                break;
            case 4:
                header->length = (word & 0xFFFF0000) >> 16;
                header->device_number = (word & 0xFF00) >> 8;
                header->trigger_flags = (word & 0xFF);
                break;
            case 5:
                header->self_trigger_word = word;
                break;
            case 6:
                header->beam_trigger_time &= 0x00000000FFFFFFFFll;
                header->beam_trigger_time |= (uint64_t)((uint64_t)word << 32);
                break;
            case 7:
                header->beam_trigger_time &= 0xFFFFFFFF00000000ll;
                header->beam_trigger_time |= word;
                break;
            case 8:
                header->led_trigger_time &= 0x00000000FFFFFFFFll;
                header->led_trigger_time |= (uint64_t)((uint64_t)word << 32);
                break;
            case 9:
                header->led_trigger_time &= 0xFFFFFFFF00000000ll;
                header->led_trigger_time |= word;
                break;
            case 10:
                header->ct_time &= 0x00000000FFFFFFFFll;
                header->ct_time |= (uint64_t)((uint64_t)word << 32);
                break;
            case 11:
                header->ct_time &= 0xFFFFFFFF00000000ll;
                header->ct_time |= word;
                break;
            case 12:
                header->crc = word;
                break;
            default:
                // Should never get here...should flag an error. TODO
                builder_log(LOG_ERROR, "This should never happen, call Tony, %i, 0x%x", which_word, word);
        }
}

void fontus_handle_bad_header(FontusTrigHeader* header) {
    builder_log(LOG_ERROR, "Likely error found\n"
                           "Bad magic  =  0x%x\n"
                           "Bad trig # =  %i\n"
                           "Bad length = %i\n"
                           "Bad time = %llu\n"
                           "Bad channel id = %i", header->magic_number, header->trig_number,
                                                    header->length, (unsigned long long) header->clock,
                                                    header->device_number);
    reeling = 1;
}

// Read events from read buffer. Returns 0 if a full event is read.
int fontus_read_proc(FPGA_IF* fpga, EventHeader* ret) {
    static EventInProgress event;
    static int first = 1;
    uint32_t val;

    if(first) {
        event = start_event();
        first = 0;
    }

    FontusTrigHeader* header = (FontusTrigHeader*) &event.event_header;

    while(event.header_bytes_read < FONTUS_HEADER_SIZE) {
        int word = event.header_bytes_read/sizeof(uint32_t);

        if(pop32(&(fpga->ring_buffer), &val)) {
            // Should only happen if we don't have enough data available to
            // read a 32-bit word
            return 0;
        }
        fontus_interpret_header_word(header, val, word);
        event.header_bytes_read += sizeof(uint32_t);
        *(uint32_t*)(fpga->event_buffer.data + fpga->event_buffer.num_bytes) = htonl(val);
        fpga->event_buffer.num_bytes += 4;
    } // Done reading header
    uint32_t calcd_crc = calc_fontus_header_crc(header);
    if(header->crc !=  calcd_crc || header->magic_number != FONTUS_MAGIC_VALUE) {
        printf("Expected = 0x%x\tRead = 0x%x\n", calcd_crc, header->crc);
        fontus_handle_bad_header(header);
        event = start_event(); // This event is being trashed, just start a new one.
        ring_buffer_update_event_read_pntr(&fpga->ring_buffer);
        return 0;
    }

    int bytes_in_buffer = ring_buffer_contiguous_readable(&fpga->ring_buffer);
    unsigned char* data = fpga->ring_buffer.buffer + fpga->ring_buffer.read_pointer;
    int bytes_read = 0;
    uint32_t word;
    int ret_val = 0;
    int channel_length=header->length;


    // FONTUS waveform reading. FONTUS outputs 4 waveforms.
    // We'll exit this loop either when we've consumed all available data, or when we've complete a single event
    while(bytes_read <= bytes_in_buffer) {
        // First check if the event is done
        if(event.current_channel == 4) {
            *((FontusTrigHeader*)ret) = *header;
            event = start_event();
            ret_val = 1;
            break;
        }
        if(bytes_read == bytes_in_buffer) {
            ret_val = 0;
            break;
        }

        // Read in a 32-bit chunk;
        word = ntohl(*(uint32_t*)(data+bytes_read));
        bytes_read+=4;

        // Waveform processing happens in 2 steps.
        // First, read in the waveform header, which should always of the form 0xFFxxFFxx where xx is the channel number
        // Second is to read in the actual samples
        if(!event.wf_header_read) {
            uint32_t expectation = (0xFF00FF00 | (event.current_channel<<16) | event.current_channel);
            if(word != expectation) {
                if(!reeling) {
                    printf("Badness found 0x%x 0x%x\n", word, expectation);
                    // TODO should handle this better;
                    reeling = 1;
                }
                // This event's trash...TODO, it'd be nice to try and do some better recovery
                fpga->event_buffer.num_bytes = 0;
                event = start_event();
                return 0;
            }

            // Stash the location in memory of the start of each waveform
            int offset = FONTUS_HEADER_SIZE + event.current_channel*((channel_length+1)*4);
            *(uint32_t*)(fpga->event_buffer.data + offset) = htonl(word);
            fpga->event_buffer.num_bytes += 4;
            event.wf_header_read = 1;
            event.samples_read = 1;
        }
        else if(event.samples_read <= header->length) {
            int offset = FONTUS_HEADER_SIZE + event.current_channel*((channel_length+1)*4) + event.samples_read*4;
            *(uint32_t*)(fpga->event_buffer.data + offset) = htonl(word);
            fpga->event_buffer.num_bytes += 4;

            if(event.samples_read == header->length) {
                event.current_channel += 1;
                event.samples_read = 0;
                event.wf_header_read = 0;
            }
            else {
                event.samples_read += 1;
            }
        }
    }
    ring_buffer_update_read_pntr(&fpga->ring_buffer, bytes_read);
    ring_buffer_update_event_read_pntr(&fpga->ring_buffer);
    return ret_val;
}

void ceres_display_event(const EventHeader* ev) {
    CeresTrigHeader* h = (CeresTrigHeader*)ev;
    builder_log(LOG_INFO, "Event trig number =  %u\n"
                          "Event length = %u\n"
                          "Event time = %llu\n"
                          "device id = %u\n"
                          "CRC  = 0x%x", h->trig_number, h->length,
                                           h->clock, h->device_number,
                                           h->crc, built_counter);
}

void fontus_display_event(const EventHeader* ev) {
    FontusTrigHeader* h = (FontusTrigHeader*)ev;
    builder_log(LOG_INFO, "Magic = 0x%x\n"
                          "Event trig number =  %u\n"
                          "Time = %llu\n"
                          "Length =  %u\n"
                          "DeviceID =  %u\n"
                          "Flags =  %u\n"
                          "Self trigger word =  %u\n"
                          "beam_trigger_time =  %llu\n"
                          "LED trigger time  =  %llu\n"
                          "CT time  =  %llu\n"
                          "CRC = 0x%x\n",
                          h->magic_number, h->trig_number, h->clock, h->length, h->device_number, h->trigger_flags,
                          h->self_trigger_word, h->beam_trigger_time,
                          h->led_trigger_time, h->ct_time, h->crc);
}

void ceres_handle_bad_header(CeresTrigHeader* header) {
    uint8_t expected_crc = calc_ceres_header_crc(header);
    builder_log(LOG_ERROR, "Likely error found\n"
                           "Bad magic  =  0x%x\n"
                           "Bad trig # =  %i\n"
                           "Bad length = %i\n"
                           "Bad time = %llu\n"
                           "Bad channel id = %i\n"
                           "Bad CRC = %i, expected = %i\n", header->magic_number, header->trig_number,
                                                    header->length, (unsigned long long) header->clock,
                                                    header->device_number, header->crc, expected_crc);
    reeling = 1;
}

void ceres_interpret_header_word(CeresTrigHeader* header, const uint32_t word, const int which_word) {
        switch(which_word) {
            case 0:
                header->magic_number = word;
                break;
            case 1:
                header->trig_number = word;
                break;
            case 2:
                header->clock &= 0x00000000FFFFFFFF;
                header->clock |= (uint64_t)((uint64_t)word << 32);
                break;
            case 3:
                header->clock &= 0xFFFFFFFF00000000;
                header->clock |= word;
                break;
            case 4:
                header->length = (word & 0xFFFF0000) >> 16;
                header->device_number = (word & 0xFF00) >> 8;
                header->crc = (word & 0xFF);
                //header->length += 1;
                break;
            default:
                // Should never get here...should flag an error. TODO
                builder_log(LOG_ERROR, "This should never happen, call Tony");
        }
}

// Read events from read buffer. Returns 0 if a full event is read.
int ceres_read_proc(FPGA_IF* fpga, EventHeader* ret) {
    static EventInProgress event;
    static int first = 1;
    uint32_t val;

    if(first) {
        event = start_event();
        first = 0;
    }
    CeresTrigHeader* header = (CeresTrigHeader*)&event.event_header;

    // TODO move this header reading shit to its own function
    while(event.header_bytes_read < CERES_HEADER_SIZE) {
        int word = event.header_bytes_read/sizeof(uint32_t);

        if(pop32(&(fpga->ring_buffer), &val)) {
            // Should only happen if we don't have enough data available to
            // read a 32-bit word
            return 0;
        }
        ceres_interpret_header_word(header, val, word);
        event.header_bytes_read += 4;
        // Copy this word into the event buffer
        *(uint32_t*)(fpga->event_buffer.data + fpga->event_buffer.num_bytes) = htonl(val);
        fpga->event_buffer.num_bytes += 4;
    } // Done reading header

    // Check the header's CRC
    // TODO this if statement will get called whenver a read is done...should only
    // happen just after the header is completely read
    uint8_t expected_crc = calc_ceres_header_crc(header);
    if(header->crc != expected_crc ||
            header->magic_number != CERES_MAGIC_VALUE) {
        builder_log(LOG_ERROR, "BAD HEADER HAPPENED");
        ceres_handle_bad_header(header);
        event = start_event(); // This event is being trashed, just start a new one.
        ring_buffer_update_event_read_pntr(&fpga->ring_buffer);
        // (TODO! maybe try and recover the event)
        return 0;
    }

    if(((header->length)+2)*4*NUM_CHANNELS > EVENT_BUFFER_SIZE) {
        // Make sure we have enough space available
        builder_log(LOG_ERROR, "Event too large to fit in memory, something's probably wrong\n");
        reeling = 1;
        return 0;
    }

    int is_even = (header->device_number % 2) == 0;
    int bytes_in_buffer = ring_buffer_contiguous_readable(&fpga->ring_buffer);
    unsigned char* data = fpga->ring_buffer.buffer + fpga->ring_buffer.read_pointer;
    int bytes_read = 0;
    uint32_t word;
    int ret_val = 0;
    int channel_length=header->length;

    // We'll exit this loop either when we've consumed all available data, or when we've complete a single event
    while(bytes_read <= (bytes_in_buffer-3)) {
        // First check if the event is done
        if(event.current_channel == NUM_CHANNELS) {
            *((CeresTrigHeader*)ret) = *header;
            event = start_event();
            ret_val = 1;
            break;
        }
        if(bytes_read == bytes_in_buffer) {
            ret_val = 0;
            break;
        }

        // Read in a 32-bit chunk;
        word = ntohl(*(uint32_t*)(data+bytes_read));
        bytes_read+=4;

        // Waveform processing happens in 3 steps.
        // First, read in the waveform header, which should always of the form 0xFFxxFFxx where xx is the channel number
        // Second is to read in the actual samples they will either be compess (6 samples per 32-bits) or uncompessed (2 samples per 32-bits)
        // Finally, read in the waveform CRC, which will be a single 32-bit number calculated on the above samples.
        if(!event.wf_header_read) {

            uint32_t expectation = (0xFF00FF00 | (event.current_channel<<16) | event.current_channel);
            if(word != expectation) {
                if(!reeling) {
                    printf("Badness found %i 0x%x 0x%x\n", header->length, word, expectation);
                    // TODO should handle this better;
                    reeling = 1;
                }
                // This event's trash...TODO, it'd be nice to try and do some better recovery
                fpga->event_buffer.num_bytes = 0;
                event = start_event();
                return 0;
            }

            // Stash the location in memory of the start of each waveform
            stash_with_reorder(&fpga->event_buffer, word, event.current_channel, 0, channel_length, is_even);
            event.wf_header_read = 1;
            event.wf_crc_read = 0;
            event.samples_read = 0;
        }
        else if(event.samples_read < header->length) {
                int valid_bit = (word & 0x80000000) >> 16;
                int compression_bit = word & 0x40000000;

                int16_t sample;
                uint32_t sample_pair=0;
                // Check the compression bit;
                if(compression_bit) {
                    // Compressed samples
                    int i;
                    for(i=5; i>=0; i--) {
                        sample = ((word & (0x1F<<(i*5))) >>(i*5));
                        // The sample is a 5-byte signed integer.
                        // The below line forces the sign bit into the correct place for it
                        // to get correctly interpreted, than shift the number back down to get the maginitude right
                        sample = ((int8_t) (sample<<3))>>3;
                        sample = event.prev_sample + sample;
                        event.prev_sample = sample;

                        if(i%2) {
                            // Only every other sample should get marked with the valid bit
                            sample |= valid_bit;
                        }

                        // "word" holds two 16-bit samples
                        sample_pair <<= 16;
                        sample_pair |= sample;

                        if((i%2) == 0) {
                            // Words get added to the buffer in pairs
                            stash_with_reorder(&fpga->event_buffer, sample_pair, event.current_channel, ++event.samples_read, channel_length, is_even);

                        }
                    }
                }
                else {
                    if(event.samples_read == 0) {
                        sample = (word >> 16) & 0x3fff;
                    }
                    else {
                        sample = event.prev_sample + (((int16_t)(word >> 14)) >> 2);
                    }

                    // Store this briefly
                    event.prev_sample = sample;

                    // Put the sample in its 32-bit pair in the upper half
                    sample_pair = sample | valid_bit;
                    sample_pair <<= 16;

                    // Calculate the second sample of the pair
                    sample = event.prev_sample + (((int16_t)((word & 0x3FFF) << 2)) >> 2);
                    event.prev_sample = sample;

                    sample_pair |= sample;

                    // Put the ADC word in the event buffer, with channel re-ordering applie
                    stash_with_reorder(&fpga->event_buffer, sample_pair, event.current_channel, ++event.samples_read, channel_length, is_even);
                }
        }
        else if(!event.wf_crc_read) {
            // Read the CRC
            stash_with_reorder(&fpga->event_buffer, word, event.current_channel, event.samples_read+1, channel_length, is_even);
            event.current_channel += 1;
            event.samples_read = 0;
            event.wf_crc_read = 1;
            event.wf_header_read = 0;
            event.prev_sample = 0;
        }
    }

    // Done reading stuff update the ring buffer
    ring_buffer_update_read_pntr(&fpga->ring_buffer, bytes_read);
    ring_buffer_update_event_read_pntr(&fpga->ring_buffer);
    return ret_val;
}

int fontus_validate_crcs(const EventHeader* header, const EventBuffer* eb) {
    // Currently don't need to do anything the header is validated during the
    // read process, and there's no other data that can be validated (right now).
    (void)header;
    (void)eb;
    return 0;
}

int ceres_validate_crcs(const EventHeader* header, const EventBuffer* eb) {
    // Don't need to look at the header CRC, b/c that's validated during the
    // ceres_read_proc function. So lets not bother checking it again and only
    // check the waveform CRCs
    uint32_t calculated_crcs[NUM_CHANNELS];
    uint32_t given_crcs[NUM_CHANNELS];
    int i;
    int ret = 0;
    CeresTrigHeader* head = (CeresTrigHeader*)header;
    calculate_waveform_crcs(head, eb, calculated_crcs, given_crcs);
    for(i=0; i < NUM_CHANNELS; i++) {
        if(calculated_crcs[i] != given_crcs[i]) {
            ret = 1;
            builder_log(LOG_ERROR, "Event %i Channel %i CRC does not match", head->trig_number, i);
            builder_log(LOG_ERROR, "Calculated = 0x%x, Given = 0x%x", calculated_crcs[i], given_crcs[i]);
        }
    }
    return ret;
}

void fontus_update_stats(ProcessingStats* the_stats, EventHeader* header) {
    the_stats->trigger_id = header->fontus.trig_number;
    the_stats->latest_timestamp = header->fontus.clock;
    the_stats->device_id = header->fontus.device_number;
}

void ceres_update_stats(ProcessingStats* the_stats, EventHeader* header) {
    the_stats->trigger_id = header->ceres.trig_number;
    the_stats->latest_timestamp = header->ceres.clock;
    the_stats->device_id = header->ceres.device_number;
}

void receive_manager_io(ManagerIO* manager_command, int* in_pipe) {
    int fd = *in_pipe;
    if(fd < 0) {
        return;
    }
    manager_command->command = CMD_NONE;
    ssize_t nbytes = read(fd, manager_command, sizeof(ManagerIO));

    if(nbytes == 0) {
        // Indicates that the other side of the pipe hung up, end of file
        // Close the pipe and avoid reading from it again in the future
        // TODO I need to find some way to remove it from the "select" FD_SET
        close(fd);
        *in_pipe = -1;
        return;
    }
    // EAGAIN indicates that the socket would block except that it's set to
    // NON-BLOCK. Should mean there's no data available to read.
    if((nbytes < 0 && errno==EAGAIN)) {
        return;
    }
    if(nbytes < 0) {
        // unexpected error Error, not sure why this would happen
        // TODO figure out how to handle this better
        builder_log(LOG_ERROR, "RECEIVED %i bytes", nbytes);
        close(fd);
        *in_pipe = -1;
    }
}

void respond_to_manager_io(ManagerIO* manager_command, int* out_pipe) {
    int fd = *out_pipe;
    if(fd < 0) {
        return;
    }
    int nbytes = 0;
    do {
        nbytes += write(fd, ((char*)manager_command)+nbytes, sizeof(ManagerIO)-nbytes);
    } while(nbytes > 0 && nbytes != sizeof(ManagerIO));
    if(nbytes == 0) {
        // Not sure how to handle this
    }
    if(nbytes < 0) {
        builder_log(LOG_ERROR, "Error responding to manager command. %s", strerror(errno));
        close(fd);
        *out_pipe = -1;
    }
    manager_command->command = CMD_NONE;
}

int reset_connection(FPGA_IF* fpga_if, const char* ip_addr) {
    // Close the TCP connection
    close(fpga_if->fd);
    usleep(50000); // Sleep for 0.05s just to make sure the connection actually closes

    do {
        // Send a TCP reset_command
        if(fpga_if->udp_client && send_tcp_reset(fpga_if->udp_client)) {
            builder_log(LOG_ERROR, "Error sending TCP reset. Will retry.");
            sleep(1);
            continue;
        }
        fpga_if->fd = connect_to_fpga(ip_addr);

        if(fpga_if->fd < 0) {
            builder_log(LOG_ERROR, "error ocurred connecting to FPGA. Will retry.");
            sleep(1);
        }
    } while(fpga_if->fd < 0 && loop);

    return fpga_if->fd < 0 ? 1 : 0;
}

struct BuilderConfig default_builder_config(void) {
    struct BuilderConfig config;
    config.ip = "192.168.84.192";
    config.num_events = 0;
    config.dry_run = 0;
    config.do_not_save = 0;
    config.verbosity = 0;
    config.output_filename = "/dev/null";
    config.error_filename = DEFAULT_ERROR_LOG_FILENAME;
    config.redis_host = DEFAULT_REDIS_HOST;
    config.in_pipe = -1; // Non-valid file descriptor
    config.out_pipe = -1; // Non-valid file descriptor
    config.exit_now = 0;
    return config;
}

// Run the data builder, data is interpreted, managed, and published as
// specified by the BuilderProtocol
int data_builder_main(struct BuilderConfig config) {
    int event_ready;
    struct timeval prev_time, current_time;
    const double REDIS_STATS_COOLDOWN = 1e6; // 1-second in micro-seconds
    double last_status_update_time = 0;
    ProcessingStats the_stats;
    EventHeader event_header;

    // Zero out the IO command, default behavior is NONE command
    ManagerIO manager_command;
    memset(&manager_command, 0, sizeof(manager_command));

    // Adjust the verbosity according to the configuration
    builder_verbosity_stdout = builder_verbosity_stdout + config.verbosity;
    builder_verbosity_file = builder_verbosity_file + config.verbosity;
    builder_verbosity_redis = builder_verbosity_redis + config.verbosity;

    builder_verbosity_stdout = builder_verbosity_stdout > LOG_ERROR ? LOG_ERROR : builder_verbosity_stdout;
    builder_verbosity_stdout = builder_verbosity_stdout < LOG_NEVER ? LOG_NEVER : builder_verbosity_stdout;
    builder_verbosity_file = builder_verbosity_file > LOG_ERROR ? LOG_ERROR : builder_verbosity_file;
    builder_verbosity_file = builder_verbosity_file < LOG_NEVER ? LOG_NEVER : builder_verbosity_file;
    builder_verbosity_redis = builder_verbosity_redis > LOG_ERROR ? LOG_ERROR : builder_verbosity_redis;
    builder_verbosity_redis = builder_verbosity_redis < LOG_NEVER ? LOG_NEVER : builder_verbosity_redis;

    const uint32_t HEADER_MAGIC_VALUE = config.ceres_builder ?
                                        CERES_MAGIC_VALUE    :
                                        FONTUS_MAGIC_VALUE;
    const unsigned int HEADER_SIZE = config.ceres_builder ?
                                     CERES_HEADER_SIZE    :
                                     FONTUS_HEADER_SIZE;
    const char* my_name = config.ceres_builder ?
                          "ceres_data_builder" :
                          "fontus_data_builder";


    struct BuilderProtocol protocol;
    if(config.ceres_builder) {
        protocol.reader_process = ceres_read_proc;
        protocol.display_process = ceres_display_event;
        protocol.write_event = ceres_write_to_disk;
        protocol.validate_event = ceres_validate_crcs;
        protocol.publish_event = publish_event;
        protocol.update_stats = ceres_update_stats;
    }
    else {
        protocol.reader_process = fontus_read_proc;
        protocol.display_process = fontus_display_event;
        protocol.write_event = fontus_write_to_disk;
        protocol.validate_event = fontus_validate_crcs;
        protocol.publish_event = publish_event;
        protocol.update_stats = fontus_update_stats;
    }

    initialize_stats(&the_stats);

    printf("FPGA IP set to %s\n", config.ip);
    setup_logger(my_name, config.redis_host, config.error_filename,
                 builder_verbosity_stdout, builder_verbosity_file, builder_verbosity_redis,
                 LOG_MESSAGE_MAX);
    the_logger->add_newlines = 1;


    FPGA_IF fpga_if;
    // initialize memory locations
    initialize_ring_buffer(&(fpga_if.ring_buffer));
    initialize_event_buffer(&(fpga_if.event_buffer));

    // Set the I/O pipes to non-block
    {
        int pipe_flags = fcntl(config.in_pipe, F_GETFL);
        fcntl(config.in_pipe, F_SETFL, O_NONBLOCK | pipe_flags);

        pipe_flags = fcntl(config.out_pipe, F_GETFL);
        fcntl(config.out_pipe, F_SETFL, O_NONBLOCK | pipe_flags);
    }

    fpga_if.udp_client = NULL;
    if(!config.dry_run) {
        while(1) {
            fpga_if.udp_client = connect_fakernet_udp_client(config.ip);
            if(fpga_if.udp_client) {
                break;
            }

            // This is a stupid hack to make sure a manager process doesn't hang
            // if it requests data to a builder that is stuck trying to connect
            // to an FPGA. TODO I really should come up with a better solution.
            receive_manager_io(&manager_command, &config.in_pipe);
            if(manager_command.command != CMD_NONE) {
                manager_command.arg = 0;
                respond_to_manager_io(&manager_command, &config.out_pipe);
            }
            sleep(1);
        }
    }

    // connect to FPGA
    do {
        // Send a TCP reset_command
        if(fpga_if.udp_client && send_tcp_reset(fpga_if.udp_client)) {
            builder_log(LOG_ERROR, "Error sending TCP reset. Will retry.");
            sleep(1);
            continue;
        }
        fpga_if.fd = connect_to_fpga(config.ip);

        if(fpga_if.fd < 0) {
            builder_log(LOG_ERROR, "error ocurred connecting to FPGA. Will retry.");
            sleep(1);
        }
    } while(fpga_if.fd < 0);
    builder_log(LOG_INFO, "FPGA TCP connection made");
    the_stats.connected_to_fpga = fpga_if.fd > 0 ? 1 : 0;

    builder_log(LOG_INFO, "Expecting %i channels of data per event.", NUM_CHANNELS);

    // Open file to write events to
    if(!config.do_not_save) {
        builder_log(LOG_INFO, "Opening %s for saving data", config.output_filename);
        fdisk = fopen(config.output_filename, "wb");

        if(!fdisk) {
            builder_log(LOG_ERROR, "error opening file: %s", strerror(errno));
            return 0;
        }
    }

    // Need to know which file descriptor has the largest numerical value,
    // because "select" needs to use that value
    const int max_pipe = fpga_if.fd > config.in_pipe ? fpga_if.fd+1 : config.in_pipe+1;

    gettimeofday(&prev_time, NULL);
    // TODO (important!), this should use the config structure, not a hardcoded string
    redis = create_redis_unix_conn("/var/run/redis/redis-server.sock");
    usleep(100000); // Give redis time to connect

    // Do authentication
    // Immediatly free the reply cause I don't care about it
    if(redis) {
        freeReplyObject(redisCommand(redis, "AUTH numubarnuebar"));
    }

    // TODO, use sigaction instead of signal
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

#ifdef DUMP_DATA
    fdump = fopen("DUMP.dat", "wb");
#endif
    // Main readout loop
    builder_log(LOG_INFO, "Entering main loop");
    event_ready = 0;
    int did_warn_about_reeling = 0;
    while(loop) {
#if __unix__ && DO_QUICKACK
        {
            int yes = 1;
            if( setsockopt(fpga_if.fd, IPPROTO_TCP, TCP_QUICKACK, &yes, sizeof(yes)) ) {
                builder_log(LOG_ERROR, "Error setting TCP_QUICKACK");
            }
        }
#endif

        // If there's no data to process, we wait for data to show up.
        // Don't block forever though so stats can continue to be updates
        if(fpga_if.ring_buffer.is_empty) {
            fd_set readfds;
            struct timeval _timeout;
            _timeout.tv_sec = 0;
            _timeout.tv_usec = 100000; // 0.1 seconds
            FD_ZERO(&readfds);
            FD_SET(fpga_if.fd, &readfds);
            FD_SET(config.in_pipe, &readfds);
            // TODO, should also try and detect disconnect here
            select(max_pipe, &readfds, NULL, NULL, &_timeout);
        }

        receive_manager_io(&manager_command, &config.in_pipe);
        switch(manager_command.command) {
            case CMD_NONE:
                // Do nothing
                break;
            case CMD_CONNECTED:
                // TODO This command seems kinda useless.
                // I don't respond to manager commands anywhere except here,
                // and I don't really have a good mechanism for "disconnecting".
                // In principle this may change as I expand the functionality
                manager_command.arg = the_stats.connected_to_fpga;
                break;
            case CMD_ISREELING:
                manager_command.arg = reeling ? 1 : 0;
                break;
            case CMD_NUMBUILT:
                manager_command.arg = the_stats.event_count;
                break;
            case CMD_RESET_CONN:
                builder_log(LOG_WARN, "Resetting TCP connection");
                manager_command.arg = reset_connection(&fpga_if, config.ip);
                // If the ret value is non-zero the connection failed
                if(!manager_command.arg) {
                    builder_log(LOG_WARN, "Re-connected");
                }
                break;
            default:
                builder_log(LOG_WARN, "Unknown command %i recieved");
                manager_command.command = (int32_t)CMD_NONE;
        }
        if(manager_command.command != CMD_NONE) {
            respond_to_manager_io(&manager_command, &config.out_pipe);
        }

        pull_from_fpga(&fpga_if);
        if(reeling) {
            the_stats.reeling_happened = 1;
            if(!did_warn_about_reeling) {
                builder_log(LOG_ERROR, "Reeeling");
            }
            reeling = !find_event_start(&fpga_if, HEADER_MAGIC_VALUE);
            did_warn_about_reeling = reeling;
        }
        else {
            event_ready = protocol.reader_process(&fpga_if, &event_header);
            if(did_warn_about_reeling) {
                builder_log(LOG_INFO, "Recovered from reeling");
            }
            did_warn_about_reeling = 0;
        }

        gettimeofday(&current_time, NULL);
        the_stats.uptime = (current_time.tv_sec*1e6 + current_time.tv_usec) - the_stats.start_time;

        if((the_stats.uptime - last_status_update_time) > REDIS_STATS_COOLDOWN) {
            the_stats.fifo_wpointer = fpga_if.ring_buffer.write_pointer;
            the_stats.fifo_event_rpointer = fpga_if.ring_buffer.event_read_pointer;
            the_stats.fifo_rpointer = fpga_if.ring_buffer.read_pointer;
            redis_publish_stats(redis, &the_stats);
            the_stats.reeling_happened = 0;
            last_status_update_time = the_stats.uptime;
        }

        if(event_ready) {
            protocol.validate_event(&event_header, &fpga_if.event_buffer);
            protocol.publish_event(redis, fpga_if.event_buffer, HEADER_SIZE);
            prev_time = current_time;
            built_counter += 1;
            protocol.display_process(&event_header);
            if(!config.do_not_save) {
                protocol.write_event(&fpga_if.event_buffer, &event_header);
            }
            the_stats.event_count++;
            protocol.update_stats(&the_stats, &event_header);

            if(config.num_events != 0 && the_stats.event_count >= config.num_events) {
                builder_log(LOG_INFO, "Collected %i events...exiting", the_stats.event_count);
                end_loop();
            }

            // Finally clear the event buffer
            fpga_if.event_buffer.num_bytes = 0;
        }
    }
#ifdef DUMP_DATA
    fclose(fdump);
#endif
    clean_up();
    close(fpga_if.fd);
    return 0;
}
