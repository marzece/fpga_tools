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
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include "hiredis/hiredis.h"
#include "fnet_client.h"
#include "daq_logger.h"

#if __linux__
#define htonll(x) ((((uint64_t)htonl(x)) << 32) + htonl((x) >> 32))
#define ntohll(x) htonll(x)
#endif


int verbosity_stdout = LOG_INFO;
int verbosity_redis = LOG_WARN;
int verbosity_file = LOG_WARN;

// Counter for how many events have been built
int built_counter = 0;
// Number of channels to be readout
int NUM_CHANNELS = 16;

// Largest possible size for single message (in byte)
#define LOG_MESSAGE_MAX 1024

#define DEFAULT_REDIS_HOST  "127.0.0.1"
#define DEFAULT_ERROR_LOG_FILENAME "data_builder_error_log.log"


// Every event header starts with this magic number
#define MAGIC_VALUE 0xFFFFFFFF
#define HEADER_SIZE 20 // 128-bits aka 16 bytes

// Space that should be allocated for data buffers
#define BUFFER_SIZE (10*1024*1024) // 10 MB
#define EVENT_BUFFER_SIZE BUFFER_SIZE

uint32_t crc32(uint32_t crc, uint32_t * buf, unsigned int len);
void crc8(unsigned char *crc, unsigned char m);

// FILE handle for writing to disk
static FILE* fdisk = NULL;
// Log file

// Redis connection for logging & data (TODO could seperate those functionalities)
redisContext* redis = NULL;

// Variable for deciding to stay in the main loop or not.
// When loop is zero program should exit soon after.
int loop = 1;

// If reeling==1 need to search for next trigger header magic value.
int reeling = 0;

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
    long num_bytes;
} EventBuffer;

typedef struct ProcessingStats {
    unsigned int event_count;
    unsigned int trigger_id;
    double start_time; // In microseconds (since Epoch start)
    double uptime; // In microseconds
    unsigned int pid;
    int connected_to_fpga;
    int fifo_rpointer;
    int fifo_event_rpointer;
    int fifo_wpointer;

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

typedef struct TrigHeader{
    uint32_t magic_number;
    uint32_t trig_number;
    uint64_t clock;
    uint16_t length;
    uint8_t device_number;
    uint8_t crc;
} TrigHeader;

typedef struct ChannelHeader {
    uint8_t reserved1;
    uint8_t channel_id1;
    uint8_t reserved2;
    uint8_t channel_id2;
} ChannelHeader;


// This handles keeping track of reading an event while in the middle of it
typedef struct EventInProgress {
    TrigHeader event_header;
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
    const int port = 1; // FPGA doesn't use ports, so this doesn't matter
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

    // Idk if this is needed!
    //int yes = 1;
    //setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

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
                tv.tv_sec = 15;
                tv.tv_usec = 0;
                FD_ZERO(&myset);
                FD_SET(fd, &myset);
                res = 0;
                res = select(fd+1, NULL, &myset, NULL, &tv);
                if(res != 1) {
                    goto error;
                }
                break;
            }
            builder_log(LOG_ERROR, "Error connecting TCP socket: %s", strerror(errno));
            sleep(2);
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

    return fd;

error:
    close(fd);
    // reuse adress
    return -1;
}

// This is the function that reads data from the FPGA ethernet connection
size_t pull_from_fpga(FPGA_IF* fpga_if) {
    ssize_t bytes_recvd = 0;
    size_t contiguous_space_left;

    unsigned char* w_buffer = fpga_if->ring_buffer.buffer;
    size_t w_buffer_idx = fpga_if->ring_buffer.write_pointer;

    // TODO could consider checking total_space, not just contiguous space and doing two
    // recv's, one at the "end" then one at the start of the ring buffer.
    // That might help if this gets a lot of chump reads that are only like 100 bytes.
    contiguous_space_left = ring_buffer_contiguous_space_available(&(fpga_if->ring_buffer));
    if(contiguous_space_left > 0) {
        bytes_recvd = recv(fpga_if->fd, w_buffer + w_buffer_idx, contiguous_space_left, 0);
        if(bytes_recvd < 0) {
            //printf("Error retrieving data from socket: %s\n", strerror(errno));
            return 0;
        }
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

EventInProgress start_event() {
    EventInProgress ev;

    ev.header_bytes_read = 0;
    ev.data_bytes_read = 0;
    ev.current_channel = 0;
    ev.wf_header_read = 0;
    ev.samples_read = 0;
    ev.wf_crc_read = 0;
    ev.prev_sample = 0;

    // Fill with magic words for debugging
    ev.event_header.magic_number = 0xFEEDBEEF;
    ev.event_header.trig_number = 0XDEADBEEF;
    ev.event_header.length = 0xAABB;
    ev.event_header.clock = 0xBEEFBABE;
    ev.event_header.device_number =  0x71;
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

uint8_t calc_trig_header_crc(TrigHeader* header) {
    unsigned char crc = 0;
    crc = crc_from_bytes((unsigned char*)&header->trig_number, 4, crc);
    crc = crc_from_bytes((unsigned char*)&header->clock, 8, crc);
    crc = crc_from_bytes((unsigned char*)&header->length, 2, crc);
    crc8(&crc, header->device_number);
    return crc ^ 0x55; // The 0x55 here makes it the ITU CRC8 implemenation
}

void display_event(const TrigHeader* header) {
    builder_log(LOG_INFO, "Event trig number =  %u\n"
                          "Event length = %u\n"
                          "Event time = %llu\n"
                          "device id = %u\n"
                          "CRC  = 0x%x", header->trig_number, header->length,
                                           header->clock, header->device_number,
                                           header->crc, built_counter);
}

void clean_up() {
    builder_log(LOG_INFO, "Closing and cleaning up");
    if(fdisk) {
        builder_log(LOG_INFO, "Closing data file");
        fclose(fdisk);
    }
    cleanup_logger();
    redisFree(redis);
    redis = NULL;
}

void end_loop() {
    loop = 0;
}

void sig_handler(int signum) {
    // TODO think of more signals that would be useful
    builder_log(LOG_WARN, "Sig recieved %i", signum);
    static int num_kills = 0;
    if(signum == SIGINT || signum == SIGKILL) {
        num_kills +=1;
        end_loop();
    }
    if(num_kills >= 2) {
        exit(1);
    }
}

void write_to_disk(EventBuffer eb) {
    size_t nwritten;
    nwritten = fwrite(eb.data, 1, eb.num_bytes, fdisk);
    if(nwritten != eb.num_bytes) {
        // TODO check errno
        builder_log(LOG_ERROR, "Error writing event");
        // TODO close the file??
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

void interpret_header_word(TrigHeader* header, const uint32_t word, const int which_word) {
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

void handle_bad_header(TrigHeader* header) {
    uint8_t expected_crc = calc_trig_header_crc(header);
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

int find_event_start(FPGA_IF* fpga) {
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
        if(*current == 0xFF && *(current+1) == 0xFF && *(current+2) == 0xFF && *(current+3) == 0xFF) {
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

// Read events from read buffer. Returns 0 if a full event is read.
int read_proc(FPGA_IF* fpga, TrigHeader* ret) {
    static EventInProgress event;
    static int first = 1;
    uint32_t val;

    if(first) {
        event = start_event();
        first = 0;
    }

    // TODO move this header reading shit to its own function
    while(event.header_bytes_read < HEADER_SIZE) {
        int word = event.header_bytes_read/sizeof(uint32_t);
        TrigHeader* header = &(event.event_header);

        if(pop32(&(fpga->ring_buffer), &val)) {
            // Should only happen if we don't have enough data available to
            // read a 32-bit word
            return 0;
        }
        interpret_header_word(header, val, word);
        event.header_bytes_read += 4;
        // Copy this word into the event buffer
        *(uint32_t*)(fpga->event_buffer.data + fpga->event_buffer.num_bytes) = htonl(val);
        fpga->event_buffer.num_bytes += 4;
    } // Done reading header

    // Check the header's CRC
    // TODO this if statement will get called whenver a read is done...should only
    // happen just after the header is completely read
    if(event.event_header.crc != calc_trig_header_crc(&event.event_header) ||
            event.event_header.magic_number != 0xFFFFFFFF ||
            event.event_header.length < 50) {
        builder_log(LOG_ERROR, "BAD HEADER HAPPENED");
        handle_bad_header(&(event.event_header));
        event = start_event(); // This event is being trashed, just start a new one.
        ring_buffer_update_event_read_pntr(&fpga->ring_buffer);
        // (TODO! maybe try and recover the event)
        return 0;
    }

    if((event.event_header.length+2)*4*NUM_CHANNELS > EVENT_BUFFER_SIZE) {
        // Make sure we have enough space available
        builder_log(LOG_ERROR, "Event too large to fit in memory, something's probably wrong\n");
        return 0;

    }

    int bytes_in_buffer = ring_buffer_contiguous_readable(&fpga->ring_buffer);
    unsigned char* data = fpga->ring_buffer.buffer + fpga->ring_buffer.read_pointer;
    int bytes_read = 0;
    uint32_t word;
    int ret_val = 0;
    // We'll exit this loop either when we've consumed all available data, or when we've complete a single event
    while(bytes_read <= bytes_in_buffer) {
        // First check if the event is done
        if(event.current_channel == NUM_CHANNELS) {
            *ret = event.event_header;
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
            *(uint32_t*)(fpga->event_buffer.data + fpga->event_buffer.num_bytes) = htonl(word);
            fpga->event_buffer.num_bytes += 4;

            event.wf_header_read = 1;
            event.wf_crc_read = 0;
            event.samples_read = 0;
        }
        else if(event.samples_read < event.event_header.length) {
                int valid_bit = (word & 0x80000000) >> 16;
                int compression_bit = word & 0x40000000;

                int16_t sample;
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
                        // Only every other sample should get marked with the valid bit
                        if(i %2 == 1) {
                            sample |= valid_bit;
                        }

                        *(uint16_t*)(fpga->event_buffer.data+fpga->event_buffer.num_bytes) = htons(sample);
                        fpga->event_buffer.num_bytes += 2;
                    }
                    event.samples_read += 3;
                }
                else {
                    // not compressed samples
                    if(event.samples_read == 0) {
                        sample = (word >> 16) & 0x3fff;
                    }
                    else {
                        sample = event.prev_sample + (((int16_t)(word >> 14)) >> 2);
                    }
                    event.prev_sample = sample;
                    *(uint16_t*)(fpga->event_buffer.data+fpga->event_buffer.num_bytes) = htons(sample | valid_bit);
                    fpga->event_buffer.num_bytes += 2;

                    sample = event.prev_sample + (((int16_t)((word & 0x3FFF) << 2)) >> 2);
                    event.prev_sample = sample;
                    *(uint16_t*)(fpga->event_buffer.data + fpga->event_buffer.num_bytes) = htons(sample);
                    fpga->event_buffer.num_bytes += 2;

                    event.samples_read +=1;

                }
        }
        else if(!event.wf_crc_read) {
            // Read the CRC
            *(uint32_t*)(fpga->event_buffer.data + fpga->event_buffer.num_bytes) = htonl(word);
            fpga->event_buffer.num_bytes += 4;

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

// Send event to redis database
void redis_publish_event(redisContext*c, EventBuffer eb) {
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
    arglens[2] = HEADER_SIZE;
    r = redisCommandArgv(c, 3,  args,  arglens);
    if(!r) {
        builder_log(LOG_ERROR, "Redis error! : %s", c->errstr);
    }
    freeReplyObject(r);
}

void redis_publish_stats(redisContext* c, const ProcessingStats* stats) {
    if(!c) {
        return;
    }

    redisReply* r;
    size_t arglens[3];
    const char* args[3];

    char buf[1024];

    args[0] = "PUBLISH";
    arglens[0] = strlen(args[0]);

    args[1] = "builder_stats";
    arglens[1] = strlen(args[1]);

    arglens[2] = snprintf(buf, 1024, "%i %u %i %i %i %i", stats->event_count,
                                                          stats->trigger_id,
                                                          stats->fifo_event_rpointer,
                                                          stats->fifo_rpointer,
                                                          stats->fifo_wpointer,
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

    while(!fnet_client) {
        fnet_client = fnet_ctrl_connect(fnet_hname, reliable, &err_string, NULL);
        if(!fnet_client) {
            builder_log(LOG_ERROR, "ERROR Connecting on UDP channel: %s. Will retry", err_string);
            sleep(1);
        }
    }
    builder_log(LOG_INFO, "UDP channel connected");
    return fnet_client;
}

int send_tcp_reset(struct fnet_ctrl_client* client) {
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

enum ArgIDs {
    ARG_NONE=0,
    ARG_NUM_EVENTS,
    ARG_FILENAME,
    ARG_ERR_FILENAME,
    ARG_REDIS_HOST,
    ARG_IP,
    ARG_NUM_CHANNELS_
};

void print_help_message() {
    printf("fakernet_data_builder\n"
            "   usage:  fakernet_data_builder [--ip ip] [--out filename] [--no-save] [--num num_events]\n");
}

void calculate_channel_crcs(const TrigHeader* header, const EventBuffer* event, uint32_t* calculated_crcs, uint32_t* given_crcs) {
    int i;
    int length = header->length;
    uint32_t* wf_start = (uint32_t*)(event->data + HEADER_SIZE + 4);
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

void initialize_stats(ProcessingStats* stats) {
    struct timeval tv;
    stats->event_count = 0;
    stats->trigger_id = 0;
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

int main(int argc, char **argv) {
    int i;
    unsigned int num_events = 0;
    const char* ip = "192.168.1.192";
    enum ArgIDs expecting_value;
    int do_not_save = 0;
    const char* FOUT_FILENAME = "/dev/null";
    const char* ERROR_FILENAME = DEFAULT_ERROR_LOG_FILENAME;
    const char* redis_host = DEFAULT_REDIS_HOST;
    int event_ready;
    struct timeval prev_time, current_time;
    uint32_t calculated_crcs[NUM_CHANNELS];
    uint32_t given_crcs[NUM_CHANNELS];
    const double REDIS_DATA_STREAM_COOLDOWN = 200e3; // In micro-seconds
    const double REDIS_STATS_COOLDOWN = 1e6; // 1-second in micro-seconds
    double last_status_update_time;
    ProcessingStats the_stats;
    TrigHeader event_header;

    initialize_stats(&the_stats);
    last_status_update_time = 0;

    if(argc > 1 ) {
        expecting_value = 0;
        for(i=1; i < argc; i++) {
            if(!expecting_value) {
                if((strcmp(argv[i], "--num") == 0) || (strcmp(argv[i], "-n") == 0)) {
                    expecting_value = ARG_NUM_EVENTS;
                }
                else if((strcmp(argv[i], "--out") == 0) || (strcmp(argv[i], "-o") == 0)) {
                    expecting_value = ARG_FILENAME;
                }
                else if(strcmp(argv[i], "--ip") == 0) {
                    expecting_value = ARG_IP;
                }
                else if((strcmp(argv[i], "--num_channels") == 0) || (strcmp(argv[i], "--nchan") == 0)) {
                    expecting_value = ARG_NUM_CHANNELS_;
                }
                else if(strcmp(argv[i], "--no-save") == 0) {
                    do_not_save = 1;
                    expecting_value = 0;
                }
                else if((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-h") == 0)) {
                    // TODO should scan the whole argv for help before setting any other values
                    print_help_message();
                    return 0;
                }
                else if((strcmp(argv[i], "--err") == 0) ) {
                    expecting_value = ARG_ERR_FILENAME;
                }
                else if((strcmp(argv[i], "--redis") == 0) ) {
                    expecting_value = ARG_REDIS_HOST;
                }
                else {
                    printf("Unrecognized option \"%s\"\n", argv[i]);
                    return 0;
                }
            } else {
                switch(expecting_value) {
                    case ARG_NUM_EVENTS:
                        num_events = strtoul(argv[i], NULL, 0);
                        printf("Will be exiting after %i events\n", num_events);
                        break;
                    case ARG_FILENAME:
                        FOUT_FILENAME = argv[i];
                        printf("Will be writing to \"%s\"\n", FOUT_FILENAME);
                        break;
                    case ARG_IP:
                        ip = argv[i];
                        printf("FPGA IP set to %s\n", ip);
                        break;
                    case ARG_NUM_CHANNELS_:
                        NUM_CHANNELS = strtoul(argv[i], NULL, 0);
                        printf("FPGA IP set to %s\n", ip);
                        break;
                    case ARG_ERR_FILENAME:
                        printf("Log file set to %s\n", argv[i]);
                        ERROR_FILENAME = argv[i];
                        break;
                    case ARG_REDIS_HOST:
                        printf("Redis hostname set to %s\n", argv[i]);
                        redis_host = argv[i];
                        break;
                    case ARG_NONE:
                    default:
                        break;
                }
                expecting_value = 0;
            }
        }

        if(expecting_value) {
            printf("Did find value for last argument...exiting\n");
            return 1;
        }
    }

    FPGA_IF fpga_if;
    // initialize memory locations
    initialize_ring_buffer(&(fpga_if.ring_buffer));
    initialize_event_buffer(&(fpga_if.event_buffer));

    struct fnet_ctrl_client* udp_client = connect_fakernet_udp_client(ip);
    if(!udp_client) {
        builder_log(LOG_ERROR, "couldn't make UDP client");
        return 1;
    }

    setup_logger("fakernet_data_builder", redis_host, ERROR_FILENAME,
                 verbosity_stdout, verbosity_file, verbosity_redis,
                 LOG_MESSAGE_MAX);
    the_logger->add_newlines = 1;

    // connect to FPGA
    do {
        // Send a TCP reset_command
        if(send_tcp_reset(udp_client)) {
            printf("Sending TCP Reset");
            builder_log(LOG_ERROR, "Error sending TCP reset. Will retry.");
            sleep(2);
            continue;
        }
        fpga_if.fd = connect_to_fpga(ip);

        if(fpga_if.fd < 0) {
            builder_log(LOG_ERROR, "error ocurred connecting to FPGA. Will retry.");
            sleep(2);
        }
    } while(fpga_if.fd < 0);
    builder_log(LOG_INFO, "FPGA TCP connection made");
    the_stats.connected_to_fpga = 1;

    builder_log(LOG_INFO, "Expecting %i channels of data per event.", NUM_CHANNELS);

    // Open file to write events to
    if(!do_not_save) {
        builder_log(LOG_INFO, "Opening %s for saving data", FOUT_FILENAME);
        fdisk = fopen(FOUT_FILENAME, "wb");

        if(!fdisk) {
            builder_log(LOG_ERROR, "error opening file: %s", strerror(errno));
            return 0;
        }
    }

    gettimeofday(&prev_time, NULL);
    redis = create_redis_unix_conn("/var/run/redis/redis-server.sock");
    usleep(100000); // Give redis time to connect

    // Do authentication
    // Immediatly free the reply cause I don't care about it
    freeReplyObject(redisCommand(redis, "AUTH numubarnuebar"));

    signal(SIGINT, sig_handler);
    signal(SIGKILL, sig_handler);

    // Main readout loop
    builder_log(LOG_INFO, "Entering main loop");
    event_ready = 0;
    int did_warn_about_reeling = 0;
    while(loop) {

        pull_from_fpga(&fpga_if);
        if(reeling) {
            if(!did_warn_about_reeling) {
            builder_log(LOG_ERROR, "Reeeling");
            }
            reeling = !find_event_start(&fpga_if);
            did_warn_about_reeling = reeling;
        }
        else {
            event_ready = read_proc(&fpga_if, &event_header);
        }

        gettimeofday(&current_time, NULL);
        the_stats.uptime = (current_time.tv_sec*1e6 + current_time.tv_usec) - the_stats.start_time;

        if((the_stats.uptime - last_status_update_time) > REDIS_STATS_COOLDOWN) {
            the_stats.fifo_wpointer = fpga_if.ring_buffer.write_pointer;
            the_stats.fifo_event_rpointer = fpga_if.ring_buffer.event_read_pointer;
            the_stats.fifo_rpointer = fpga_if.ring_buffer.read_pointer;
            redis_publish_stats(redis, &the_stats);
            last_status_update_time = the_stats.uptime;
        }

        if(event_ready) {
            calculate_channel_crcs(&event_header, &fpga_if.event_buffer, calculated_crcs, given_crcs);
            for(i=0; i < NUM_CHANNELS; i++) {
                if(calculated_crcs[i] != given_crcs[i]) {
                    builder_log(LOG_ERROR, "Event %i Channel %i CRC does not match", event_header.trig_number, i);
                    builder_log(LOG_ERROR, "Calculated = 0x%x, Given = 0x%x", calculated_crcs[i], given_crcs[i]);
                }
            }
            redis_publish_event(redis, fpga_if.event_buffer);
            prev_time = current_time;
//            if(((current_time.tv_sec - prev_time.tv_sec)*1e6 + (current_time.tv_usec - prev_time.tv_usec)) > REDIS_DATA_STREAM_COOLDOWN) {
//                redis_publish_event(redis, event);
//                prev_time = current_time;
//            }
            built_counter += 1;
            display_event(&event_header);
            if(!do_not_save) {
                write_to_disk(fpga_if.event_buffer);
            }
            the_stats.event_count++;
            the_stats.trigger_id = event_header.trig_number;

            if(num_events != 0 && the_stats.event_count >= num_events) {
                builder_log(LOG_INFO, "Collected %i events...exiting", the_stats.event_count);
                end_loop();
            }

            // Finally clear the event buffer
            fpga_if.event_buffer.num_bytes = 0;
        }
    }
    clean_up();
    close(fpga_if.fd);
    return 0;
}
