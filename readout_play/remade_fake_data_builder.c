#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "triple_buffer.h"


// Absolute maximum event size is 2^14*16*4 + 16 == 2^20  +16== 1MB +16
// Now lets be real....an event that size ever happening is an error. But
// it means we're guranteed an event can always fit in 1MB of memory

int fpga_fd;
int data_fd = 0;
int resp_fd = 0;
/* okay here's what's gonna happen. We'll have 3 buffers of some large size
 * two buffers will be for reading from one for writing.
 * The reason this is needed other than just two buffers is b/c I always want
 * to write to an empyt buffer, but I dont wanna write until its full necessarily.
 * So we write a bunch of data to a buffer until it's got some small-ish amount
 * of memory left, then shift that buffer to a read buffer. And start writing the next buffer.
 * Meanwhile we begin reading data from a buffer once it's no longer a write buffer,
 * but with high probability that buffer will end 'in medias res', i.e. in the middle
 * of an event, so we can't throw the buffer out until we've finished off that event
 * and put it into some nice structure. So we need a third buffer so that
 * we ensure we finish off that 'medias res' event before recycling the read
 * buffer as a write buffer.
 */

#define HEADER_SIZE 16 //128-bits aka 16 bytes
#define NUM_CHANNELS 16
#define BUFFER_SIZE (1024*1024) // 1 MB

typedef struct Header{
    uint32_t word;
    uint32_t length;
    union { 
        // Not sure if this is legal, can I be guranteed these will ocuppy (exactly)
        // the same memory? This suggests maybe ...but only for the first variable
        // https://stackoverflow.com/questions/36824637/memory-position-of-elements-in-c-c-union
        uint64_t time;
        struct {
            uint32_t clock1;
            uint32_t clock2;
        };
    };
} Header;

// What I plan  for this is to assume sample are laid out contiguously
// except for a few jumps between different buffers. So kinda "piece-wise
// contiguous". The "locations" will point to the start of each contiguous
// chunk, and the lengths indicate how many samples are in each chunk
#define MAX_SPLITS 16
typedef struct Event {
    Header header;
    uint32_t* locations[MAX_SPLITS];
    uint32_t lengths[MAX_SPLITS];
} Event;

typedef struct EventInProgress {
    Event event;
    int header_bytes_read;
    int samples_read;
} EventInProgress;

// Open socket to FPGA returns 0 if successful
int open_data_channel() {
    const char* data_fn = "my_fake_fpga_data";
    int _recv_fd = 0;
    _recv_fd = open(data_fn, O_RDONLY);
    return _recv_fd;
}

int open_resp_channel() {
    static const char* resp_fn = "fake_fpga_resp_channel";
    int _resp_fd = 0;

    _resp_fd = open(resp_fn, O_WRONLY);
    return _resp_fd;

}

size_t pull_from_fpga() {
    //printf("writing\n");
    uint32_t resp;
    ssize_t bytes_recvd = 0;

    dbyte* write_location;
    int space_available;
    find_write_position(&write_location, &space_available);


    if(space_available > 0) {
        // TODO this should perhaps be a non-blocking read
        bytes_recvd = read(data_fd, write_location, space_available);
        //bytes_recvd = recv(fpga_fd, w_buffer + w_buffer_idx, space_left, 0);
        if(bytes_recvd < 0) {
            printf("Error retrieving data from socket: %s\n", strerror(errno));
        }
        resp = bytes_recvd;
        if(bytes_recvd) {
            write(resp_fd, &resp, sizeof(uint32_t));

            register_writes(bytes_recvd);
        }
    } else {
        rw_buffers.write_finished = 1;
    }
    return bytes_recvd;
}

EventInProgress start_event() {
    int i;
    EventInProgress ev;

    ev.header_bytes_read = 0;
    ev.samples_read = 0;
    ev.event.header.word = 0XDEADBEEF;
    ev.event.header.length = 0xDEADBABE;
    ev.event.header.clock1 = 0xFACEBEEF;
    ev.event.header.clock2 =  0xDEADFACE;
    // TODO use memset
    for(i=0; i<NUM_CHANNELS; i++ ) {
        ev.event.locations[i] = NULL;
        ev.event.lengths[i] = 0;
    }
    return ev;
}

Event last_event;
void display_event(Event* ev) {
    // Pass
     last_event  = *ev;
}

void write_to_disk(Event* ev) {
    // WIP
    static int disk_fd = 0;
    const char* FILE_NAME = "fpga_data.dat";
    if(disk_fd <= 0) {
        disk_fd = open(FILE_NAME, O_WRONLY);
        if(disk_fd <= 0) {
            printf("error opening file");
        }
    }
}

int read_proc(Event* ret) {
    static EventInProgress event;
    static int first = 1;
    int err;
    uint32_t* val_p;

    if(first) {
        event = start_event();
        first = 0;
    }

    // TODO move this header reading shit to its own function
    while(event.header_bytes_read < HEADER_SIZE) {
        int word = event.header_bytes_read/4;
        Header* header = &(event.event.header);
        switch(word) {
            case 0:
                val_p = &(header->word);
                break;
            case 1:
                val_p = &(header->length);
                break;
            case 2:
                val_p = &(header->clock1);
                break;
            case 3:
                val_p = &(header->clock2);
                break;
            default:
                // Should never get here...should flag an error. TODO
                val_p = NULL;
                printf("This should never happen, call Tony\n");
        }

        err = pop32(val_p);
        if(err) {
            // should only happen if we're at the end of the younger read buffer,
            // should flag that we're done reading.
            return -1;
        }
        event.header_bytes_read += sizeof(uint32_t);
    } // Done reading header

    if(event.event.header.length != 1000) {
       printf("Likely error found\n");
       printf("Bad word =  %i\n", event.event.header.word);
       printf("Bad length = %i\n", event.event.header.length);
       printf("Bad time = %i\n", event.event.header.clock1);
       exit(1);
    }

    int bufnum;
    int r_queue_idx;
    int r_queue_len;
    if(find_read_position(&bufnum, &r_queue_idx, &r_queue_len)) {
        // no more to read
        rw_buffers.read_finished = 1;
        return 0;
    }

    int event_length = event.event.header.length;
    int samples_to_read = event_length*NUM_CHANNELS;
    int samples_remaining = samples_to_read - event.samples_read;
    uint32_t* read_location = (uint32_t*)(rw_buffers.buffers[bufnum] + r_queue_idx);

    // This should be guranateed to be evenly divisible by 4 aka sizeof(uin32t)
    int samples_in_buffer = (r_queue_len - r_queue_idx)/sizeof(uint32_t);
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
    event.event.locations[i] = read_location;

    if(samples_remaining < samples_in_buffer) {
        // The rest of this event is available in the current read buffer
        event.event.lengths[i] = samples_remaining; // TODO this length is is units of samples (should be bytes?)
        event.samples_read += samples_remaining;
        rw_buffers.buff_idxs[bufnum] += samples_remaining*sizeof(uint32_t);
        // Now that the event is finished I need to tell someone!
        *ret = event.event;
        event = start_event();
        return 1;
    }

    event.event.lengths[i] = samples_in_buffer; // TODO this length is is units of samples (should be bytes?)
    event.samples_read += samples_in_buffer;
    rw_buffers.buff_idxs[bufnum] += samples_in_buffer*sizeof(uint32_t);
    return 0;
}

int main(int argc, char **argv) {

    // initialize memory locations

    // connect to FPGA
    //fpga_fd = connect_to_fpga();
     while(data_fd <= 0 ) {
         data_fd = open_data_channel();
     }
     while(resp_fd <=0) {
         resp_fd = open_resp_channel();
     }

    int fpga_fd = resp_fd && data_fd;
    if(fpga_fd < 0) {
        printf("error ocurred connecting to fpga\n");
        return 0;
    }

    Event event;
    int event_ready;

    // TODO, I should have the reader update this to make sure the next "new"
    // buffer will have enough bytes to finish off the event!
    initialize_buffers();

    printf("Main Loop\n");
    while(1) {
        // Would like to ensure that no more than one read proc or write proc
        // happens in each loop. I.e. you can read once and write once but if you
        // want/need to read or write again, you should just loop. This
        // ensure no event is "skipped" in readout

        event_ready = 0;
        int write_buf_fullness = rw_buffers.buff_lens[rw_buffers.state];
        //int write_empty = write_buf_fullness == 0;
        //int elder_bufnum  = (rw_buffers.state + 2) % 3;

        if(!rw_buffers.write_finished) {
            pull_from_fpga();
        }
        if(!rw_buffers.read_finished) {
            event_ready = read_proc(&event);
        }

        if(write_buf_fullness >= SWAP_THRESHOLD && rw_buffers.read_finished) {

            // If we're done reading, do a swap...TODO in principle this could
            // be done before reading is "finished" we just need to complete
            // the last event from the "elder" buffer. Once that's done we can
            // do a swap if need be. But for now it's a bit easier to just do
            // a swap only when reading is fully done
            //
            // (Shower thought) maybe I should implement a refence counter type
            // system for each buffer so when an event uses memory it keeps a reference
            // count, but when that event gets read out it removes a count...
            shift_buffers();
        }

        if(event_ready) {
            // TODO add more fun things
            display_event(&event);
        }

    }
    return 0;
}
