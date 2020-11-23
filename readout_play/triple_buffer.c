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

int SWAP_THRESHOLD = 0.10*BUFFER_SIZE;

typedef uint8_t dbyte;
typedef struct TripleBuffer {
    dbyte* buffers[3];
    int buff_idxs[3];
    int buff_lens[3];
    int read_finished;
    int write_finished;
    int state;
} TripleBuffer;

TripleBuffer rw_buffers;
int last_swap_was_dirty;

// Returns the first available memory location that can be written to
// and the remaining length of write buffer
void find_write_position(dbyte** position, int* len) {
    int bufnum = rw_buffers.state;
    int w_buffer_idx = rw_buffers.buff_idxs[bufnum];

    dbyte* w_buffer = rw_buffers.buffers[bufnum] + w_buffer_idx;
    space_left = BUFFER_SIZE - w_buffer_idx;

    *position = w_buffer;
    *len = space_left;
}

// Returns 0 if the buffer has not been fully read and fills in
// bufnum, idx, and len with the buffer, index and remaining space.
// Otherwise if the buffer is "empty" it returns non-zero
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

int register_writes(const int n){
    int bufnum = rw_buffers.state;
    int w_buffer_idx = rw_buffers.buff_idxs[bufnum];

    int space_left = BUFFER_SIZE - w_buffer_idx;
    if(space_left < n) {
        // Not even going to do a partial write
        return -1;
    } 
    if(space_left == n) {
        rw_buffers.write_finished = 1;
    }
    rw_buffers.buff_idxs[bufnum] += n;
    rw_buffers.buff_lens[bufnum] += n;
    return 0;
}

int register_reads(const int n) {
    int bufnum, idx, len, n_left;
    int empty = find_read_position(&bufnum, &idx, &len);

    int first_r_buffer = bufnum == (rw_buffers.state +1) % 3;
    int space_left = len - idx;

    if(empty && n>0) {
        rw_buffers.read_finished = 1;
        return -1;
    }

    if(n <= space_left) {
        rw_buffers.buff_idxs[bufnum] += n;
        if(n == space_left) {
            rw_buffers.read_finished = 1;
        }
        return 0;

    } else if(first_r_buffer){
        // Fill up the first buffer then register the remaining bytes for
        // the second buffer
        n_left = n - len;
        rw_buffers.buff_idxs[bufnum] += space_left;
        return register_reads(n_left); // recursively do the second buffer
    }
    // If here we're in the second buffer and there's not enough space to
    // register that many writes.....
    return -1;

}

void shift_buffers() {
    //printf("shifting\n");
    // This function does the buffer swap where it moves the oldest read buffer
    // to become the write buffer. The old write buffer becomes the new read buffer,
    // and the previously "young" read buffer becomes the oldest read buffer.
    // And finally to make my life easier, I ensure the newest read buffer
    // ends on a even multiple of 4 bytes. This makes parcelling into 32-bit words
    // easier.
    // Any dangling bytes are moved to the start of the new write buffer
    int buf_state = rw_buffers.state;
    int w_bufnum = buf_state;
    unsigned char* w_buffer = rw_buffers.buffers[w_bufnum];
    int w_length = rw_buffers.buff_lens[w_bufnum];
    int off_by;

    int new_w_bufnum = (buf_state + 1) % 3;
    unsigned char* new_write_buffer = rw_buffers.buffers[new_w_bufnum];

    // First clear out the "top" read buffer, it will be the new write buffer
    rw_buffers.buff_lens[new_w_bufnum] = 0;

    // Move the index for the new read buffer and the new write buffer to zero
    // for the 'old' read buffer the index doesn't move
    rw_buffers.buff_idxs[w_bufnum] = 0;
    rw_buffers.buff_idxs[new_w_bufnum] = 0;

    // Now move any dangling bytes at the end of the (old) write_buffer
    // to the start of the new write buffer
    last_swap_was_dirty = 0;
    off_by = w_length % 4;
    while(off_by > 0) {
        last_swap_was_dirty = 1;
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

int pop32(uint32_t* val) {
    int bufnum, r_queue_idx, r_queue_len;
    unsigned char* r_buffer = NULL;
    uint32_t* pntr = NULL;

    // Make sure a NULL pntr wasn't passed in
    if(!val) {
        return -1;
    }

    // Find our current read_position in the read_buffer.
    // If we're at the end of the buffer, just return;
    if(find_read_position(&bufnum, &r_queue_idx, &r_queue_len)) {
        // Should only return non-zero if at the end of the read buffer
        rw_buffers.read_finished = 1;
        return -1;
    }
    // If the buffer is not "empty" we're 'guaranteed' to have at least 4 bytes
    // of space available, so we don't need to do any partial reads
    r_buffer = rw_buffers.buffers[bufnum];
    pntr = (uint32_t*)(r_buffer + r_queue_idx);
    *val = *pntr;
    rw_buffers.buff_idxs[bufnum] += sizeof(uint32_t);
    return 0;
}

int swap_ready() {

    int write_buf_fullness = rw_buffers.buff_lens[rw_buffers.state];

    return write_buf_fullness >= SWAP_THRESHOLD && rw_buffers.read_finished
}


}
