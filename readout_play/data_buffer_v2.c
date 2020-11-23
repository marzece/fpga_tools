/*
 * data_buffer.c
 *
 *  Created on: Nov 19, 2019
 *      Author: marzece
 */
#include "data_buffer_v2.h"
#include <string.h> // For memcpy
#include <stdint.h>

uint16_t push_to_buffer(uint32_t val) {
    uint32_t* pntr = (uint32_t*)(data_buffer.buffer + data_buffer.w_pointer);

    if(data_buffer.count==0) {
        // Handle the case of writing to an empty buffer specially
        // I shift the r/w pointers to zero as an optimization
        data_buffer.r_pointer = data_buffer.w_pointer = 0;
        *pntr = val; // NTS this is wrong
        data_buffer.w_pointer = 4;
        data_buffer.count = 4;
        return 0;
    }
    if(data_buffer.count+4 > DATA_BUF_LEN) {
        // Buffer will be full cannot write this word
        return 1;
    }
    // For here on out, we can assume there exists enough space to write the value
    data_buffer.count += 4;
    int next = data_buffer.w_pointer + 4;
    if(next > DATA_BUF_LEN) {
        uint16_t off_by = next - DATA_BUF_LEN;
        uint16_t num_at_end = 4 - off_by;

        memcpy(pntr, (char*)&val, num_at_end);
        pntr = (uint32_t*) data_buffer.buffer;
        memcpy(pntr, (char*)&val + num_at_end, off_by);
        data_buffer.w_pointer = off_by;
        return 0;
    }
    // Potential speed up here is possible if next is a uint16_t instead
    // of an int however it only works if data_buffer size is 2^16
    if(next == DATA_BUF_LEN) {
        next = 0;
    }

    *pntr = val;
    data_buffer.w_pointer = next;
    return 0;
}

// If some number of words were directly read from the
// data buffer, use this function to appropriately
// update the r_pointer.
// Note this doesn't check that the read_pointer stays
// behind the write_pointer....that's up to the caller

// Also, TODO, check & test this lol
void data_buffer_register_reads(uint32_t n) {
    data_buffer.r_pointer += n;
    data_buffer.count -= n;
    // This isn't needed for data length of 2^16, but incase that's ever
    // not the case we'll leave this (compiler should remove it anyways)
    if(data_buffer.r_pointer > DATA_BUF_LEN) {
        data_buffer.r_pointer -= DATA_BUF_LEN;
    }

    if(data_buffer.r_pointer == data_buffer.w_pointer) {
        //IF the buffer is empty, shift the r/w pointers to zero
        // so that we can maximize contiguous space available
        data_buffer.r_pointer = 0;
        data_buffer.w_pointer = 0;
    }
}

void data_buffer_register_writes(uint32_t n) {
    if(data_buffer.count == 0) {
        data_buffer.w_pointer = 0;
        data_buffer.r_pointer = 0;
    }
    data_buffer.w_pointer += n;
    data_buffer.count += n;
    if(data_buffer.w_pointer > DATA_BUF_LEN) {
        data_buffer.w_pointer -= DATA_BUF_LEN;
    }

}

void initialize_data_buffer() {
    data_buffer.r_pointer = 0;
    data_buffer.w_pointer = 0;
    data_buffer.count = 0;
}

uint32_t data_buffer_space_available() {
    return DATA_BUF_LEN - data_buffer.count;
}

uint32_t data_buffer_space_used() {
    return data_buffer.count;
}

uint32_t data_buffer_contiguous_space_available() {
    if(data_buffer.w_pointer > data_buffer.r_pointer && data_buffer.count) {
            return DATA_BUF_LEN  - data_buffer.w_pointer;
    }
    if(data_buffer.w_pointer < data_buffer.r_pointer) {
        return data_buffer.r_pointer - data_buffer.w_pointer;
    }
    if(data_buffer.count) {
        return 0;
    }
    return DATA_BUF_LEN;
}

// TODO improve this
uint32_t data_buffer_contiguous_space_used() {
    if(data_buffer.w_pointer > data_buffer.r_pointer) {
        return data_buffer.count;
    }
    if(data_buffer.w_pointer < data_buffer.r_pointer) {
        return DATA_BUF_LEN  - data_buffer.r_pointer;
    }
    //read pointer == write_pointer
    if(data_buffer.count) { //
        // Buffer is full
        return DATA_BUF_LEN  - data_buffer.r_pointer;
    }
    //Buff is empty
    return 0;
}
