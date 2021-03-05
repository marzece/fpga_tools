#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include "hiredis/hiredis.h"
#include "fnet_client.h"

// Absolute maximum event size is 2^14*16*4 + 16 == 2^20  +16== 1MB +16
// This limit comes from the FIFO depth on the FPGA.
// Now lets be real....an event that size ever happening is an error. But
// it means we're guranteed an event can always fit in 1MB of memory

// File descriptor for FPGA ethernet comms
static int fpga_fd;
// FILE handle for writing to disk
static FILE* fdisk = NULL; 

// Variable for deciding to stay in the main loop or not.
// When loop is zero program should exit soon after.
int loop = 1;

/* okay here's basically what's going on here.
 * We have 3 buffers of some large size,
 * two buffers will be for reading from, one for writing.
 * The reason this is needed rather than just two buffers is b/c I always want
 * to write to an empty buffer, but I dont necessarily want to keep  writing until its full.
 * So we write a bunch of data to a buffer until it's got some small-ish amount
 * of memory left, then shift that buffer to a read buffer.
 * And then start writing the next buffer.
 * Meanwhile, we begin processing data from a read  buffer as soon as it's no longer a write buffer,
 * but with high probability that buffer will end 'in medias res', i.e. in the middle
 * of an event, so we can't throw the buffer out until we've finished off that event
 * and put it into some nice structure. So we need a third buffer so that
 * we ensure we finish off that 'medias res' event before recycling the read
 * buffer as a write buffer.
 * All of this relies on the assumption that an event will never be larger
 * than a single memory buffer.
 */

#define HEADER_SIZE 20 // 128-bits aka 16 bytes
#define NUM_CHANNELS 8
#define BUFFER_SIZE (1024*1024) // 1 MB

// Write Buffer = buffers[buf_state]
// read_buffer1 = buffers[(buf_state + 1) % 3]
// read_buffer2 = buffers[(buf_state + 2) % 3 ]
typedef struct TripleBuffer {
    char* buffers[3];
    int buff_idxs[3];
    int buff_lens[3];
    int read_finished;
    int write_finished;
    int state;
} TripleBuffer;

TripleBuffer rw_buffers;

#define MAGIC_VALUE 0xFFFFFFFF
typedef struct TrigHeader{
    uint32_t magic_number;
    uint32_t trig_number;
    uint64_t clock;
    uint16_t length;
    uint8_t device_number;
    uint8_t reserved;
} TrigHeader;
typedef struct ChannelHeader {
    uint8_t channel_id;
} ChannelHeader;

// What I plan for this is to assume sample are laid out contiguously
// except for a few jumps between different buffers. So kinda "piece-wise
// contiguous". The "locations" will point to the start of each contiguous
// chunk, and the lengths indicate how many samples are in each chunk
#define MAX_SPLITS 16
typedef struct Event {
    TrigHeader header;
    uint32_t* locations[MAX_SPLITS];
    uint32_t lengths[MAX_SPLITS];
} Event;

typedef struct EventInProgress {
    Event event;
    int header_bytes_read;
    int samples_read;
} EventInProgress;

// Open socket to FPGA returns 0 if successful
int connect_to_fpga() {
    const char* fpga_ip = "192.168.1.192";
    const int port = 1;
    struct sockaddr_in fpga_addr;
    fpga_addr.sin_family = AF_INET;
    fpga_addr.sin_addr.s_addr = inet_addr(fpga_ip);
    fpga_addr.sin_port = htons(port);

    int fd = socket(AF_INET, SOCK_STREAM, 0); 
    if(fd < 0) {
        // TODO check errno and give a proper error message
        return fd;
    }
    // TODO...add a timeout to this
    if(connect(fd, (struct sockaddr*)&fpga_addr, sizeof(fpga_addr))) {
        return -1;
    }
    return fd;
}

// This is the function that eeads data from the FPGA ethernet connection
size_t pull_from_fpga() {
    ssize_t bytes_recvd = 0;
    size_t space_left;
    int bufnum = rw_buffers.state;

    int w_buffer_idx = rw_buffers.buff_idxs[bufnum];

    char* w_buffer = rw_buffers.buffers[bufnum];
    space_left = BUFFER_SIZE - w_buffer_idx;
    if(space_left > 0) {
        // TODO this should perhaps be a non-blocking read
        bytes_recvd = recv(fpga_fd, w_buffer + w_buffer_idx, space_left, 0);
        if(bytes_recvd < 0) {
            printf("Error retrieving data from socket: %s\n", strerror(errno));
            return 0;
        }
        w_buffer_idx += bytes_recvd;
        space_left -= bytes_recvd;
    } else {
        rw_buffers.write_finished = 1;
    }
    rw_buffers.buff_idxs[bufnum] += bytes_recvd;
    rw_buffers.buff_lens[bufnum] += bytes_recvd;
    return bytes_recvd;
}

void shift_buffers() {
    // This function does the buffer swap where it moves the oldest read buffer
    // to become the write buffer. The old write buffer becomes the new read buffer,
    // and the previously "young" read buffer becomes the oldest read buffer.
    // And finally to make my life easier, I ensure the newest read buffer
    // ends on a even multiple of 4 bytes. This makes parcelling into 32-bit words
    // easier.
    // Any dangling bytes are moved to the start of the new write buffer
    int buf_state = rw_buffers.state;
    int w_bufnum = buf_state;
    char* w_buffer = rw_buffers.buffers[w_bufnum];
    int w_length = rw_buffers.buff_lens[w_bufnum];
    int off_by;

    int new_w_bufnum = (buf_state + 1) % 3;
    char* new_write_buffer = rw_buffers.buffers[new_w_bufnum];

    // First clear out the "top" read buffer, it will be the new write buffer
    rw_buffers.buff_lens[new_w_bufnum] = 0;

    // Move the index for the new read buffer and the new write buffer to zero
    // for the 'old' read buffer the index doesn't move
    rw_buffers.buff_idxs[w_bufnum] = 0;
    rw_buffers.buff_idxs[new_w_bufnum] = 0;

    // Now move any dangling bytes at the end of the (old) write_buffer
    // to the start of the new write buffer
    off_by = w_length % 4;
    while(off_by > 0) {
        new_write_buffer[rw_buffers.buff_idxs[new_w_bufnum]] = w_buffer[w_length -1 - off_by];
        
        // Move the new write buffer indices up
        rw_buffers.buff_idxs[new_w_bufnum] += 1;
        rw_buffers.buff_lens[new_w_bufnum] += 1;

        // Move the old write_buffer length back
        rw_buffers.buff_lens[w_bufnum] -= 1;

        off_by -=1;
    }


    // And finally update the buffer state
    rw_buffers.state = (rw_buffers.state + 1) % 3;
    rw_buffers.read_finished=0;
    rw_buffers.write_finished=0;
}

void initialize_buffers() {
    int i;
    for(i=0; i < 3; i++) {
        rw_buffers.buff_lens[i] = 0;
        rw_buffers.buff_idxs[i] = 0;
        rw_buffers.buffers[i] = malloc(BUFFER_SIZE);
        if(!rw_buffers.buffers[i]) {
            printf("Could not allocate enough memory!\n");
            exit(1);
        }
    }
    rw_buffers.state = 0;
    rw_buffers.write_finished = 0;
    // Since read buffer is empty to start, we're already done reading
    rw_buffers.read_finished = 1;
}

EventInProgress start_event() {
    int i;
    EventInProgress ev;

    ev.header_bytes_read = 0;
    ev.samples_read = 0;
    // Fill with magic words for debugging
    ev.event.header.magic_number = 0xFEEDBEEF;
    ev.event.header.trig_number = 0XDEADBEEF;
    ev.event.header.length = 0xAABB;
    ev.event.header.clock = 0xFACEBEEF;
    ev.event.header.device_number =  0x71;
    // TODO use memset
    for(i=0; i<MAX_SPLITS; i++ ) {
        ev.event.locations[i] = NULL;
        ev.event.lengths[i] = 0;
    }
    return ev;
}

void display_event(Event* ev) {
    printf("Event trig number =  %u\n", ev->header.trig_number);
    printf("Event length = %u\n", ev->header.length);
    printf("Event time = %u\n", ev->header.clock);
    printf("Channel id = %u\n", ev->header.device_number);
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
    const char* FILE_NAME = "fpga_data.dat";
    int nwritten, i;

    // TODO move this shit to a seperate function and call it before the main
    // loop is entered
    if(!fdisk) {
        printf("Opening %s for saving data\n", FILE_NAME);
        fdisk = fopen(FILE_NAME, "wb");

        if(!fdisk) {
            // TODO check errno
            printf("error opening file\n");
            return;
        }
    }
    // Write header
    // TODO technichally writing the TrigHeader here as a struct is not a
    // good idea, should replace with writing the header components
    // 
    // write header
    //nwritten = fwrite(&(ev->header), sizeof(TrigHeader), 1, fdisk);
    {
        nwritten = fwrite(&(ev->header.magic_number), sizeof(uint32_t), 1, fdisk);
        nwritten += fwrite(&(ev->header.trig_number), sizeof(uint32_t), 1, fdisk);
        nwritten += fwrite(&(ev->header.clock), sizeof(uint64_t), 1, fdisk);
        nwritten += fwrite(&(ev->header.length), sizeof(uint16_t), 1, fdisk);
        nwritten += fwrite(&(ev->header.device_number), sizeof(uint8_t), 1, fdisk);
        nwritten += fwrite(&(ev->header.reserved), sizeof(uint8_t), 1, fdisk);
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
        nwritten = fwrite(ev->locations[i], sizeof(uint32_t), ev->lengths[i], fdisk);
        if(nwritten != ev->lengths[i]) {
            // TODO check errno
            printf("Error writing event\n");
            // TODO close the file??
            return;
        }
    }
}

// Find the position to start reading from in read buffers
int find_read_position(int *bufnum, int *idx, int *len) {
    int _bufnum = (rw_buffers.state + 2) % 3;
    int r_queue_idx = rw_buffers.buff_idxs[_bufnum];
    int r_queue_len = rw_buffers.buff_lens[_bufnum];
    if(r_queue_len == r_queue_idx) {
        _bufnum = (rw_buffers.state + 1) % 3;
        r_queue_idx = rw_buffers.buff_idxs[_bufnum];
        r_queue_len = rw_buffers.buff_lens[_bufnum];
    }

    if(bufnum) {
        *bufnum = _bufnum;
    }
    if(idx) {
        *idx = r_queue_idx;
    }
    if(len) {
        *len = r_queue_len;
    }
    return *idx==*len;
}

// Read 32 bits from read buffer
int pop32(uint32_t* val) {
    int bufnum, r_queue_idx, r_queue_len;
    char* r_buffer = NULL;
    uint32_t* pntr = NULL;

    // Make sure a NULL pntr wasn't passed in
    if(!val) {
        return -1;
    }

    // Find our current read_position in the read_buffer.
    // If we're at the end of the buffer, just return;
    if(find_read_position(&bufnum, &r_queue_idx, &r_queue_len)) {
        // Should only return non-zero if at the end of the read buffer
        return -1;
    }

    r_buffer = rw_buffers.buffers[bufnum];
    pntr = (uint32_t*)(r_buffer + r_queue_idx);
    *val = ntohl(*pntr); // TODO fakernet doesn't actually do endian-ness....
    rw_buffers.buff_idxs[bufnum] += sizeof(uint32_t);
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
                header->clock |= (uint64_t)((uint64_t)word << 32);
                break;
            case 3:
                header->clock |= word;
                break;
            case 4:
                header->length = (word & 0xFFFF0000) >> 16;
                header->device_number = (word & 0xFF00) >> 8;
                //header->reserved = (word & 0xF);
                break;
            default:
                // Should never get here...should flag an error. TODO
                printf("This should never happen, call Tony\n");
        }
}
// Read events from read buffer. Returns 0 if a full event is read.
int read_proc(Event* ret) {
    static EventInProgress event;
    static int first = 1;
    uint32_t val;
    if(first) {
        event = start_event();
        first = 0;
    }
    // TODO move this header reading shit to its own function
    while(event.header_bytes_read < HEADER_SIZE) {
        int word = event.header_bytes_read/4;
        TrigHeader* header = &(event.event.header);

        if(pop32(&val)) {
            // should only happen if we're at the end of the younger read buffer,
            // should flag that we're done reading.
            rw_buffers.read_finished = 1;
            return -1;
        }
        interpret_header_word(header, val, word);
        event.header_bytes_read += sizeof(uint32_t);
    } // Done reading header

    printf("Got header\n 0x%x\n0x%x\n0x%x\n0x%x\n0x%x\n", event.event.header.trig_number,
                                                    (uint32_t)(event.event.header.clock & 0xFFFFFFFF),
                                                    (uint32_t)(event.event.header.clock >> 32),
                                                    event.event.header.device_number,
                                                    event.event.header.length);

    if(event.event.header.length > 2000 || event.event.header.length < 10 ||
       event.event.header.magic_number != MAGIC_VALUE) {
        printf("Likely error found\n");
        printf("Bad magic  =  0x%x\n", event.event.header.magic_number);
        printf("Bad trig # =  %i\n", event.event.header.trig_number);
        printf("Bad length = %i\n", event.event.header.length);
        printf("Bad time = %llu\n", (unsigned long long)event.event.header.clock);
        printf("Bad channel id = %i\n", event.event.header.device_number);
        end_loop();
        return 0;
    }



    int bufnum;
    int r_queue_idx;
    int r_queue_len;
    if(find_read_position(&bufnum, &r_queue_idx, &r_queue_len)) {
        // no more to read
        rw_buffers.read_finished = 1;
        return 0;
    }


    int event_length = event.event.header.length + 1;
    //int samples_to_read = event_length;
    int samples_to_read = event_length*NUM_CHANNELS;

    // There's one 32-bit 'header' for each channel, b/c I'm lazy I'm just
    // gonna treat them like extra samples and try to make sure they get ignored later
    //samples_to_read += NUM_CHANNELS;

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

// Connect to redis database
redisContext* create_redis_conn() {
    static const char* redis_hostname = "127.0.0.1";
    printf("Opening Redis Connection\n");

    redisContext* c;
    c = redisConnect(redis_hostname, 6379);
    if(c == NULL || c->err) {
        printf("Redis connection error %s\n", c->errstr);
        printf("Continuing\n");
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

    // TODO should add a check to make sure the below NEVER happens twice
    if(!mem) {
        mem = malloc(MEM_SIZE);
    }
    // TODO, instead of putting byte count increments by hand should do sizeof()
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
    memcpy(mem + byte_count, &(event->header.reserved), sizeof(uint8_t));
    byte_count += 1;
    for(i=0;i < MAX_SPLITS; i++) {
        if(!event->locations[i]) {
            break;
        }
        memcpy(mem+byte_count, event->locations[i], event->lengths[i]*sizeof(uint32_t));


        byte_count += event->lengths[i]*sizeof(uint32_t);
    }
    return mem;
}

// Send event to redis database
void redis_publish_event(redisContext*c, const Event event) {

    redisReply* r;
    // TrigHeader header;
    // uint32_t* locations[MAX_SPLITS];
    // uint32_t lengths[MAX_SPLITS];
    size_t arglens[3];
    const char* args[3];

    args[0] = "PUBLISH";
    arglens[0] = strlen(args[0]);
    args[1] = "event_stream";
    arglens[1] = strlen(args[1]);
    args[2] = copy_event(&event);
    arglens[2] = HEADER_SIZE + sizeof(uint32_t)*(event.header.length+1)*NUM_CHANNELS;
    //arglens[2] = sizeof(TrigHeader) + sizeof(uint32_t)*NUM_CHANNELS*event.header.length;

    r = redisCommandArgv(c, 3,  args,  arglens);
    if(!r) {
        printf("Redis error!\n");
    }

}

struct fnet_ctrl_client* connect_fakernet_udp_client() {
    const char* fnet_hname = "192.168.1.192";
    int reliable = 0; // wtf does this do?
    const char* err_string = NULL;
    struct fnet_ctrl_client* fnet_client = NULL;
    fnet_client = fnet_ctrl_connect(fnet_hname, reliable, &err_string, stderr);
    if(!fnet_client) {
        printf("ERROR Connecting!\n");
        return NULL;
    }
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

// This swaps the 16 bit values wihtin a 32-bit value i.e 0xDEADBEEF->0xBEEFDEAD
// this is cause currently fakernet reads out pairs of 16bit samples in the wrong
// order
void do_short_swap(Event* event) {
    uint32_t swap_var;
    uint32_t* pntr;
    int i, j;
    for(i=0;i < MAX_SPLITS; i++) {
        if(!event->locations[i]) {
            break;
        }
        for(j=0; j<event->lengths[i]; j++) {
            pntr = (event->locations[i] + j);
            swap_var = ((*pntr) << 16) | ((*pntr) >> 16);
            *pntr = swap_var;
        }
    }
}

int main(int argc, char **argv) {

    int num_events = 0;
    int event_count = 0;

    if(argc > 1 ) {
        if(argc != 3) {
            printf("Idk how to handle these arguments\n");
            return 0;
        }

        if(strcmp(argv[1], "--num") == 0 || strcmp(argv[1], "-n") == 0) {
            num_events = atoi(argv[2]);
            printf("Will be exiting after %i events\n", num_events);
        }


    }

    // initialize memory locations
    initialize_buffers();

    struct fnet_ctrl_client* udp_client = connect_fakernet_udp_client();
    if(!udp_client) {
        printf("couldn't make UDP client\n");
        return 1;
    }
    // Send a TCP reset_command
    if(send_tcp_reset(udp_client)) {
        return 1;
    }
    // connect to FPGA
    fpga_fd = connect_to_fpga();
    if(fpga_fd < 0) {
        printf("error ocurred connecting to fpga\n");
        return 0;
    }

    Event event;
    int event_ready;

    // TODO, I should have the reader update this to make sure the next "new"
    // buffer will have enough bytes to finish off the event!
    const int SWAP_THRESHOLD = 0.10*BUFFER_SIZE;

    redisContext* redis = create_redis_conn();

    signal(SIGINT, sig_handler);
    signal(SIGKILL, sig_handler);
    // Main readout loop
    printf("Entering main loop\n");
    while(loop) {
        // Would like to ensure that no more than one read proc or write proc
        // happens in each loop. I.e. you can read once and write once but if you
        // want/need to read or write again, you should just loop. This
        // ensure no event is "skipped" in readout
        // TODO, probably should make it so either a read or a write happens in
        // a single loop, not both.

        event_ready = 0;
        int write_buf_fullness = rw_buffers.buff_lens[rw_buffers.state];

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
            //do_short_swap(&event);
            redis_publish_event(redis, event);
            display_event(&event);
            write_to_disk(&event);
            event_count++;
            if(num_events != 0 && event_count >= num_events) {
                printf("Collected %i events...exiting\n", event_count);
                end_loop();
            }
        }

    }
    clean_up();
    return 0;
}
