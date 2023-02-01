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
#include <stdio.h>
#include <stdlib.h>
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

FILE* fdump = NULL;

// TODO need to capture some of these global state variables into a struct
// or soemthing that of nature

int verbosity_stdout = LOG_INFO;
int verbosity_redis = LOG_WARN;
int verbosity_file = LOG_WARN;

#define LOG_MESSAGE_MAX 1024

#define DEFAULT_REDIS_HOST  "127.0.0.1"
#define DEFAULT_ERROR_LOG_FILENAME "data_builder_error_log.log"


#define MAGIC_VALUE 0xF00FF00F
#define HEADER_SIZE 52
#define BUFFER_SIZE (1024*1024) // 1 MB

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

// If reeling==1 need to search for next header magic value.
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
     * If the "read_pointer" and the write_pointer are equal that can only happen if we've
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
    RingBuffer ring_buffer; // Data buffer
} FPGA_IF;

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

// This handles keeping track of reading an event while in the middle of it
typedef struct EventInProgress {
    int header_bytes_read;
    FontusTrigHeader header;
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
            sleep(5);
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

void initialize_buffer(RingBuffer* ring_buffer) {
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

EventInProgress start_event(void) {
    EventInProgress ev;
    ev.header_bytes_read = 0;
    return ev;
}

uint32_t calc_trig_header_crc(FontusTrigHeader* header) {
    // Need to layout the Header in a contiguous array so I can run the CRC
    // calculation on it. The struct is probably contigous, but better safe
    // than sorry.
    // The buffer doesn't need space for the magic_number, or the CRC though.
    char buffer[HEADER_SIZE-8];
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

void display_event(FontusTrigHeader* ev) {
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
                          ev->magic_number, ev->trig_number, ev->clock, ev->length, ev->device_number, ev->trigger_flags,
                          ev->self_trigger_word, ev->beam_trigger_time,
                          ev->led_trigger_time, ev->ct_time, ev->crc);
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

void write_to_disk(FontusTrigHeader* ev) {
    size_t nwritten;
    unsigned int byte_count = 0;
    // Write header
    {
        unsigned char header_mem[HEADER_SIZE];
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
        *((uint32_t*)(header_mem + byte_count)) = ev->self_trigger_word;
        byte_count += 4;
        *((uint64_t*)(header_mem + byte_count)) = ev->beam_trigger_time;
        byte_count += 8;
        *((uint64_t*)(header_mem + byte_count)) = ev->led_trigger_time;
        byte_count += 8;
        *((uint64_t*)(header_mem + byte_count)) = ev->ct_time;
        byte_count += 8;
        *((uint64_t*)(header_mem + byte_count)) = ev->crc;
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

void interpret_header_word(FontusTrigHeader* header, const uint32_t word, const int which_word) {
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

void handle_bad_header(FontusTrigHeader* header) {
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
        if(*current == 0xF0 && *(current+1) == 0x0F && *(current+2) == 0xF0 && *(current+3) == 0x0F) {
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
int read_proc(FPGA_IF* fpga, FontusTrigHeader* ret) {
    static EventInProgress event;
    static int first = 1;
    uint32_t val;

    if(first) {
        event = start_event();
        first = 0;
    }


    while(event.header_bytes_read < HEADER_SIZE) {
        int word = event.header_bytes_read/sizeof(uint32_t);
        FontusTrigHeader* header = &(event.header);

        if(pop32(&(fpga->ring_buffer), &val)) {
            // Should only happen if we don't have enough data available to
            // read a 32-bit word
            return 0;
        }
        interpret_header_word(header, val, word);
        event.header_bytes_read += sizeof(uint32_t);
    } // Done reading header
    uint32_t calcd_crc = calc_trig_header_crc(&event.header);
    if(event.header.crc !=  calcd_crc || event.header.magic_number != MAGIC_VALUE) {
        printf("Expected = 0x%x\tRead = 0x%x\n", calcd_crc, event.header.crc);
        handle_bad_header(&event.header);
        event = start_event(); // This event is being trashed, just start a new one.
        ring_buffer_update_event_read_pntr(&fpga->ring_buffer);
        return 0;
    }
    *ret = event.header;


    ring_buffer_update_event_read_pntr(&fpga->ring_buffer);
    event = start_event();
    return 1;
}

// Connect to redis database
redisContext* create_redis_conn(const char* hostname) {
    builder_log(LOG_INFO, "Opening Redis Connection");

    redisContext* c;
    c = redisConnect(hostname, 6379);
    if(c == NULL || c->err) {
        builder_log(LOG_ERROR, "Redis connection error %s", (c ? c->errstr : ""));
        redisFree(c);
        return NULL;
    }
    return c;
}

// Send event to redis database
void redis_publish_event(redisContext*c, const FontusTrigHeader event) {
    if(!c) {
        return;
    }
    char publish_buffer[HEADER_SIZE];
    unsigned int offset = 0;

    redisReply* r;
    size_t arglens[3];
    const char* args[3];

    args[0] = "PUBLISH";
    arglens[0] = strlen(args[0]);
    //args[1] = "trig_header_stream";
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

    arglens[2] = HEADER_SIZE;
    args[2] = publish_buffer;
    r = redisCommandArgv(c, 3,  args,  arglens);
    if(!r) {
        builder_log(LOG_ERROR, "Redis error!");
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
            sleep(3);
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
};

void print_help_message(void) {
    printf("fontus_data_builder\n"
            "   usage:  fontus_data_builder [--ip ip] [--out filename] [--no-save] [--dry]\n");
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
    int dry_run = 0;
    const char* FOUT_FILENAME = "fontus_data.dat";
    const char* ERROR_FILENAME = DEFAULT_ERROR_LOG_FILENAME;
    const char* redis_host = DEFAULT_REDIS_HOST;
    int event_ready;
    ProcessingStats the_stats;
    fdump  = fopen("DUMP.dat", "wb");
    initialize_stats(&the_stats);

    if(argc > 1 ) {
        expecting_value = 0;
        for(i=1; i < argc; i++) {
            if(!expecting_value) {
                if((strcmp(argv[i], "--out") == 0) || (strcmp(argv[i], "-o") == 0)) {
                    expecting_value = ARG_FILENAME;
                }
                else if(strcmp(argv[i], "--ip") == 0) {
                    expecting_value = ARG_IP;
                }
                else if(strcmp(argv[i], "--no-save") == 0) {
                    do_not_save = 1;
                    expecting_value = 0;
                }
                else if(strcmp(argv[i], "--dry") == 0) {
                    dry_run = 1;
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
    initialize_buffer(&(fpga_if.ring_buffer));

    struct fnet_ctrl_client* udp_client = NULL;
    if(!dry_run) {
        udp_client = connect_fakernet_udp_client(ip);
        if(!udp_client) {
            builder_log(LOG_ERROR, "couldn't make UDP client");
            return 1;
        }
    }

    setup_logger("fakernet_data_builder", redis_host, ERROR_FILENAME,
                 verbosity_stdout, verbosity_file, verbosity_redis,
                 LOG_MESSAGE_MAX);
    the_logger->add_newlines = 1;

    // connect to FPGA
    do {
        // Send a TCP reset_command
        if(udp_client && send_tcp_reset(udp_client)) {
            builder_log(LOG_ERROR, "Error sending TCP reset. Will retry.");
            sleep(5);
            continue;
        }
        fpga_if.fd = connect_to_fpga(ip);

        if(fpga_if.fd < 0) {
            builder_log(LOG_ERROR, "error ocurred connecting to FPGA. Will retry.");
            sleep(5);
        }
    } while(fpga_if.fd < 0);
    builder_log(LOG_INFO, "FPGA TCP connection made");
    the_stats.connected_to_fpga = 1;

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
    redis = create_redis_conn(redis_host);
    signal(SIGINT, sig_handler);
    signal(SIGKILL, sig_handler);

    // Main readout loop
    builder_log(LOG_INFO, "Entering main loop");
    event_ready = 0;

    int did_warn_about_reeling = 0;
    FontusTrigHeader event;
    while(loop) {

        pull_from_fpga(&fpga_if);
        if(reeling) {
            if(!did_warn_about_reeling) {
                builder_log(LOG_ERROR, "Reeeling");
            }
            reeling = !find_event_start(&fpga_if);
            did_warn_about_reeling = reeling;
        }
        event_ready = read_proc(&fpga_if, &event);

        if(event_ready) {
            redis_publish_event(redis, event);
            display_event(&event);
            if(!do_not_save) {
                write_to_disk(&event);
            }
            the_stats.event_count++;
            the_stats.trigger_id = event.trig_number;

            if(num_events != 0 && the_stats.event_count >= num_events) {
                builder_log(LOG_INFO, "Collected %i events...exiting", the_stats.event_count);
                end_loop();
            }
        }
    }
//    fclose(fdump);
    clean_up();
    close(fpga_if.fd);
    return 0;
}
