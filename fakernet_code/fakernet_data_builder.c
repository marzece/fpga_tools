#include <stdio.h>
#include <stdlib.h>
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

uint32_t crc32(uint32_t crc, uint32_t * buf, unsigned int len);
void crc8(unsigned char *crc, unsigned char m);

// FILE handle for writing to disk
static FILE* fdisk = NULL;

// Variable for deciding to stay in the main loop or not.
// When loop is zero program should exit soon after.
int loop = 1;

// If reeling==1 need to search for next trigger header magic value.
int reeling = 0;

/*
   This program handles getting data from the FPGA and reading it into a
   buffer, then processing the data by finding the start/end of each event.
   Also checks the various CRC's to ensure the data is good.

   The primary data structure for storing data while processing it is a ring
   buffer. There's one slightly non-stanard element to the ring buffer as
   implemented here, I use two different "read_pointers", one read_pointer only
   updates by moving from event start to event start, and it should never point
   to data thats in the middle of an event*. The second read pointer can point
   anywhere and is used for looking at data while you search for an events end.
   Once the end of an event is found, the event-by-event read pointer is moved up.
   Data can only be written into the ring buffer upto the event-by-event read pointer.

   Event data is by just remembering the location of the start in the memory
   buffer, and the event length. B/c an event can wrap around the end of the
   memory buffer an event can end up being split, in which case the location of
   the start is recorded, and the location of the second "start". In principle
   that second start will always be at memory buffer location 0.

   Once a full event is recorded, the memory for it is dispatched to a redis
   stream and also written to disk.

   In the event that something bad happens and a new event's header is wrong, or
   a new event isn't found immediatly after the end of the previous event the program
   is set into "reeling" mode. In that mode the program just scans through values looking
   for the start of a new event (demarcated by the 32-bit word 0xFFFFFFFF). Once that is
   found the program exits "reeling" mode and resumes normal data processing.
 */

#define MAGIC_VALUE 0xFFFFFFFF
#define HEADER_SIZE 20 // 128-bits aka 16 bytes
#define NUM_CHANNELS 16
#define BUFFER_SIZE (1024*1024) // 1 MB

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
        printf("DATA was overwritten probably!!!!\n"
                "This error is not handled so you should probably just restart things"
                "...and figure out how this happened\n");
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
        printf("Invalid data was read!!!\n"
                "This really should not have happened."
                " Everything will probably be wrong from here on out\n");
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

// What I plan for this is to assume sample are laid out contiguously
// except for a few jumps between different buffers. So kinda "piece-wise
// contiguous". The "locations" will point to the start of each contiguous
// chunk, and the lengths indicate how many bytes are in each chunk
#define MAX_SPLITS 16
typedef struct Event {
    TrigHeader header;
    uint32_t* locations[MAX_SPLITS];
    uint32_t lengths[MAX_SPLITS];
} Event;

// This handles keeping track of reading an event while in the middle of it
typedef struct EventInProgress {
    Event event;
    int header_bytes_read;
    int data_bytes_read;
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
        printf("Error creating TCP socket: %s\n", strerror(errno));
        return fd;
    }

    // Set the socket to non-block before connecting
    args  = fcntl(fd, F_GETFL, NULL);
    if(args < 0) {
        printf("Error getting socket opts\n");
        goto error;

    }
    args |= O_NONBLOCK;

    // Idk if this is needed!
    //int yes = 1;
    //setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if(fcntl(fd, F_SETFL, args) < 0) {
        printf("Error setting socket opts\n");
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
            printf("Error connecting TCP socket: %s\n", strerror(errno));
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
        printf("Error setting socket opts2\n");
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
        printf("Could not allocate enough space for data buffer!\n");
        exit(1);
    }
}

EventInProgress start_event() {
    EventInProgress ev;

    ev.header_bytes_read = 0;
    ev.data_bytes_read = 0;
    // Fill with magic words for debugging
    ev.event.header.magic_number = 0xFEEDBEEF;
    ev.event.header.trig_number = 0XDEADBEEF;
    ev.event.header.length = 0xAABB;
    ev.event.header.clock = 0xBEEFBABE;
    ev.event.header.device_number =  0x71;
    memset(ev.event.locations, 0, sizeof(uint32_t*)*MAX_SPLITS);
    memset(ev.event.lengths, 0, sizeof(uint32_t)*MAX_SPLITS);
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

void display_event(Event* ev) {
    printf("Event trig number =  %u\n", ev->header.trig_number);
    printf("Event length = %u\n", ev->header.length);
    printf("Event time = %llu\n", ev->header.clock);
    printf("device id = %u\n", ev->header.device_number);
    printf("CRC  = 0x%x\n", ev->header.crc);
}

void clean_up() {
    printf("Cleaning up\n");
    if(fdisk) {
        printf("Closing data file\n");
        fclose(fdisk);
    }
    // TODO close the redis connection too
}

void end_loop() {
    loop = 0;
}

void sig_handler(int signum) {
    // TODO think of more signals that would be useful
    printf("Sig recieved %i\n", signum);
    static int num_kills = 0;
    if(signum == SIGINT || signum == SIGKILL) {
        num_kills +=1;
        end_loop();
    }
    if(num_kills >= 2) {
        exit(1);
    }
}

void write_to_disk(Event* ev) {
    size_t nwritten;
    int i;
    // Write header
    {
        nwritten = fwrite(&(ev->header.magic_number), sizeof(uint32_t), 1, fdisk);
        nwritten += fwrite(&(ev->header.trig_number), sizeof(uint32_t), 1, fdisk);
        nwritten += fwrite(&(ev->header.clock), sizeof(uint64_t), 1, fdisk);
        nwritten += fwrite(&(ev->header.length), sizeof(uint16_t), 1, fdisk);
        nwritten += fwrite(&(ev->header.device_number), sizeof(uint8_t), 1, fdisk);
        nwritten += fwrite(&(ev->header.crc), sizeof(uint8_t), 1, fdisk);
    }
    if(nwritten != 6) {
        // TODO check errno (does fwrite set errno?)
        printf("Error writing event header!\n");
        // TODO do I want to close the file here?
        return;
    }

    for(i=0;i < MAX_SPLITS; i++) {
        if(!ev->locations[i]) {
            break;
        }
        nwritten = fwrite(ev->locations[i], 1, ev->lengths[i], fdisk);
        if(nwritten != ev->lengths[i]) {
            // TODO check errno
            printf("Error writing event\n");
            // TODO close the file??
            return;
        }
    }
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
                printf("This should never happen, call Tony\n");
        }
}

void handle_bad_header(TrigHeader* header) {
    printf("Likely error found\n");
    printf("Bad magic  =  0x%x\n", header->magic_number);
    printf("Bad trig # =  %i\n", header->trig_number);
    printf("Bad length = %i\n", header->length);
    printf("Bad time = %llu\n", (unsigned long long)header->clock);
    printf("Bad channel id = %i\n", header->device_number);
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
int read_proc(FPGA_IF* fpga, Event* ret) {
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
        TrigHeader* header = &(event.event.header);

        if(pop32(&(fpga->ring_buffer), &val)) {
            // Should only happen if we don't have enough data available to
            // read a 32-bit word
            return 0;
        }
        interpret_header_word(header, val, word);
        event.header_bytes_read += sizeof(uint32_t);
    } // Done reading header

    // Check the header's CRC
    // TODO this if_statement will get called whenver a read is done...should only
    // happen just after the header is completely read
    if(event.event.header.crc != calc_trig_header_crc(&event.event.header)) {
        printf("BAD HEADER HAPPENED");
        handle_bad_header(&(event.event.header));
        event = start_event(); // This event is being trashed, just start a new one.
        ring_buffer_update_event_read_pntr(&fpga->ring_buffer);
        // (TODO! maybe try and recover the event)
        return 0;
    }


    // The plus two is b/c there's one extra 32-bit word for the channel header,
    // than another extra from the channel CRC32.
    int event_length = event.event.header.length + 2; // Units = Num samples
    int event_total_data_bytes = event_length*NUM_CHANNELS*sizeof(uint32_t);

    // There's one 32-bit 'header' for each channel, b/c I'm lazy I'm just
    // gonna treat them like extra samples and try to make sure they get ignored later
    //samples_to_read += NUM_CHANNELS;

    int bytes_remaining = event_total_data_bytes - event.data_bytes_read;
    unsigned char * read_location = fpga->ring_buffer.buffer + fpga->ring_buffer.read_pointer;

    int bytes_in_buffer = ring_buffer_contiguous_readable(&fpga->ring_buffer);
    if(bytes_in_buffer == 0 && bytes_remaining != 0) {
        return 0;
    }

    int i;
    for(i=0; i<MAX_SPLITS; i++) {
        if(!event.event.locations[i]) {
            break;
        }
    }
    if(event.event.locations[i] != NULL) {
        printf("Error that I don't know how to handle!\n");
        exit(1);
    }

    // If this new location is going to be contiguous wiht the previous location
    // then I should just append to that one instead of creating a new "split"
    if(i>0 && (unsigned char*)event.event.locations[i-1] + event.event.lengths[i-1] == read_location) {
        i-=1;
    }
    else {
        event.event.locations[i] = (uint32_t*)read_location;
        event.event.lengths[i] = 0;
    }

    // If the space in the buffer is larger than the data required to finish the event
    // then read enough bytes to finish the event.
    if(bytes_remaining <= bytes_in_buffer) {
        // The rest of this event is available in the current read buffer
        event.event.lengths[i] += bytes_remaining;
        event.data_bytes_read += bytes_remaining;
        ring_buffer_update_read_pntr(&fpga->ring_buffer, bytes_remaining);
        // Now that the event is finished I need to tell someone!
        *ret = event.event;
        event = start_event();
        ring_buffer_update_event_read_pntr(&fpga->ring_buffer);
        return 1;
    }
    // else this buffer doesn't have enough data to finish the event

    event.event.lengths[i] += bytes_in_buffer;
    event.data_bytes_read += bytes_in_buffer;
    ring_buffer_update_read_pntr(&fpga->ring_buffer, bytes_in_buffer);
    return 0;
}

// Connect to redis database
redisContext* create_redis_conn() {
    static const char* redis_hostname = "192.168.84.99";
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

// Copies an event to one contiguous chunk of data
char* copy_event(const Event* event) {
    static const int MEM_SIZE = 1024*1024;
    static char* mem = NULL;
    size_t byte_count = 0;
    int i;

    if(!mem) {
        mem = malloc(MEM_SIZE);
    }

    // TODO this needs to protect itself from overflows!!!!!
    //memcpy(mem, &(event->header), sizeof(TrigHeader));
    memcpy(mem, &(event->header.magic_number), sizeof(uint32_t));
    byte_count += 4;
    memcpy(mem + byte_count, &(event->header.trig_number), sizeof(uint32_t));
    byte_count += 4;
    memcpy(mem + byte_count, &(event->header.clock), sizeof(uint64_t));
    byte_count += 8;
    memcpy(mem + byte_count, &(event->header.length), sizeof(uint16_t));
    byte_count += 2;
    memcpy(mem + byte_count, &(event->header.device_number), sizeof(uint8_t));
    byte_count += 1;
    memcpy(mem + byte_count, &(event->header.crc), sizeof(uint8_t));
    byte_count += 1;
    for(i=0;i < MAX_SPLITS; i++) {
        if(!event->locations[i]) {
            break;
        }
        memcpy(mem+byte_count, event->locations[i], event->lengths[i]);
        byte_count += event->lengths[i];
    }
    return mem;
}

// Send event to redis database
void redis_publish_event(redisContext*c, const Event event) {
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
    args[2] = copy_event(&event);
    arglens[2] = HEADER_SIZE + sizeof(uint32_t)*(event.header.length+2)*NUM_CHANNELS;

    r = redisCommandArgv(c, 3,  args,  arglens);
    if(!r) {
        printf("Redis error!\n");
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
        printf("Error sending stats update to redis\n");
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
            printf("ERROR Connecting on UDP channel: %s.\nWill retry\n", err_string);
            sleep(3);
        }
    }
    printf("UDP channel connected\n");
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
        printf("Error happened while doing TCP-Reset\n");
        return -1;
    }
    return 0;
}

enum ArgIDs {
    ARG_NONE=0,
    ARG_NUM_EVENTS,
    ARG_FILENAME,
    ARG_IP
};

void print_help_message() {
    printf("fakernet_data_builder\n"
            "   usage:  fakernet_data_builder [--ip ip] [--out filename] [--no-save] [--num num_events]\n");
}

int calculate_channel_crcs(Event* event, uint32_t *calculated_crcs, uint32_t* given_crcs) {
    // TODO this function is a bit of a mess....should re-think things about
    // how this is dones or how data from events is laid out in memory

    int num_samples = event->header.length;
    int num_consumed = 0;

    int header_is_next = 1;
    int i = 0;

    uint32_t current_crc = 0;
    int go_to_next_split = 0;
    int go_to_next_channel = 0;
    int ichan = 0;

    uint32_t *current = event->locations[0];
    uint32_t* end_of_split = event->locations[0] + event->lengths[0]/sizeof(uint32_t);
    uint32_t* end_of_channel = event->locations[0] + num_samples + 2;

    memset(calculated_crcs, 0, sizeof(uint32_t)*NUM_CHANNELS);
    memset(given_crcs, 0, sizeof(uint32_t)*NUM_CHANNELS);

    while(i < MAX_SPLITS) {
        if(go_to_next_channel) {
            ichan += 1;
            header_is_next = 1;
            num_consumed = 0;
            current_crc = 0;
            go_to_next_channel = 0;
            if(ichan >= NUM_CHANNELS) {
                break;
            }
        }

        if(go_to_next_split) {
            i += 1;
            if(i >= MAX_SPLITS) {
                printf("Reached the end of memory before finding all CRCs!\n");
                break;
            }
            go_to_next_split = 0;
            current = event->locations[i];
            end_of_split = event->locations[i] + event->lengths[i]/sizeof(uint32_t);
        }

        end_of_channel = current + num_samples + 2 - num_consumed;
        // Which will end sooner, the "split" or the channel's samples
        int split_ends_first = end_of_split < end_of_channel;
        uint32_t* end = split_ends_first ? end_of_split : end_of_channel;
        int num_to_read = end - current;

        if(header_is_next) {
            if(num_to_read > 1) {
                //printf("Header = 0x%x\n", *current);
                current += 1;
                num_to_read -= 1;
                num_consumed += 1;
                header_is_next = 0;
            } else if(num_to_read == 1) {
                header_is_next = 0;
                go_to_next_split = 1;
                num_consumed += 1;
                // Need to move to next split
                // (shouldn't be possible for "end" to be the end of the channel
                // unless the trigger length is 0 (which might happen?? hopefully not))
                continue;
            } else {
                // Need to move to next split
                // (shouldn't be possible for "end" to be the end of the channel
                // unless the trigger length is 0 (which might happen?? hopefully not))
                go_to_next_split = 1;
                continue;
            }
        }

        // Compute CRCs
        if(!split_ends_first) {
            num_to_read -= 1; // Don't want to calculate the CRC on the CRC
        }

        current_crc = crc32(current_crc, current, num_to_read*sizeof(uint32_t));

        num_consumed += num_to_read;
        current += num_to_read;

        if(!split_ends_first) {
            given_crcs[ichan] = ntohl(*current);
            calculated_crcs[ichan] = current_crc;
            current += 1;
            current_crc = 0;
            go_to_next_channel = 1;
            continue;
        }
        go_to_next_split = 1;
    }

    return 0;
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
    const char* FOUT_FILENAME = "fpga_data.dat";
    int event_ready;
    struct timeval prev_time, current_time;
    uint32_t calculated_crcs[NUM_CHANNELS];
    uint32_t given_crcs[NUM_CHANNELS];
    const double REDIS_DATA_STREAM_COOLDOWN = 200e3; // In micro-seconds
    const double REDIS_STATS_COOLDOWN = 1e6; // 1-second in micro-seconds
    double last_status_update_time;
    ProcessingStats the_stats;
    Event event;

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
                else if(strcmp(argv[i], "--no-save") == 0) {
                    do_not_save = 1;
                    expecting_value = 0;
                }
                else if((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-h") == 0)) {
                    // TODO should scan the whole argv for help before setting any other values
                    print_help_message();
                    return 0;
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

    struct fnet_ctrl_client* udp_client = connect_fakernet_udp_client(ip);
    if(!udp_client) {
        printf("couldn't make UDP client\n");
        return 1;
    }

    // connect to FPGA
    do {
        // Send a TCP reset_command
        if(send_tcp_reset(udp_client)) {
            printf("Error sending TCP reset. Will retry.\n");
            sleep(5);
            continue;
        }
        fpga_if.fd = connect_to_fpga(ip);

        if(fpga_if.fd < 0) {
            printf("error ocurred connecting to FPGA. Will retry.\n");
            sleep(5);
        }
    } while(fpga_if.fd < 0);
    printf("FPGA TCP connection made\n");
    the_stats.connected_to_fpga = 1;

    // Open file to write events to
    if(!do_not_save) {
        printf("Opening %s for saving data\n", FOUT_FILENAME);
        fdisk = fopen(FOUT_FILENAME, "wb");

        if(!fdisk) {
            printf("error opening file: %s\n", strerror(errno));
            return 0;
        }
    }


    gettimeofday(&prev_time, NULL);
    redisContext* redis = create_redis_conn();
    signal(SIGINT, sig_handler);
    signal(SIGKILL, sig_handler);

    // Main readout loop
    printf("Entering main loop\n");
    event_ready = 0;
    while(loop) {

        pull_from_fpga(&fpga_if);
        if(reeling) {
            printf("Reeeling\n");
            reeling = !find_event_start(&fpga_if);
        }
        event_ready = read_proc(&fpga_if, &event);

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
            // TODO add more fun things
            calculate_channel_crcs(&event, calculated_crcs, given_crcs);
            for(i=0; i < NUM_CHANNELS; i++) {
                if(calculated_crcs[i] != given_crcs[i]) {
                    printf("Event %i Channel %i CRC does not match\n", event.header.trig_number, i);
                    printf("Calculated = 0x%x, Given = 0x%x\n", calculated_crcs[i], given_crcs[i]);
                }
            }
            if(((current_time.tv_sec - prev_time.tv_sec)*1e6 + (current_time.tv_usec - prev_time.tv_usec)) > REDIS_DATA_STREAM_COOLDOWN) {
                redis_publish_event(redis, event);
                prev_time = current_time;
            }
            display_event(&event);
            if(!do_not_save) {
                write_to_disk(&event);
            }
            the_stats.event_count++;
            the_stats.trigger_id = event.header.trig_number;

            if(num_events != 0 && the_stats.event_count >= num_events) {
                printf("Collected %i events...exiting\n", the_stats.event_count);
                end_loop();
            }
        }
    }
    clean_up();
    close(fpga_if.fd);
    return 0;
}
