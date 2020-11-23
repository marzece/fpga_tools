/*
 * data_buffer.c
 *
 *  Created on: Nov 19, 2019
 *      Author: marzece
 */
#include "data_buffer.h"
#include <string.h> // For memcpy
#include <stdint.h>

uint16_t push_to_buffer(uint32_t val) {
    uint32_t* pntr = (uint32_t*)(data_buffer.buffer + data_buffer.w_pointer);

    if(data_buffer.r_pointer == data_buffer.w_pointer) {
        // Handle the case of writing to an empty buffer specially
        // I shift the r/w pointers to zero as an optimization
        data_buffer.r_pointer = data_buffer.w_pointer = 0;
        *pntr = val; // NTS this is wrong
        data_buffer.w_pointer = 4;
        return 0;
    }

    uint16_t next = data_buffer.w_pointer + 4;
    // Which is more optimal an if or a modulus?
    if(next > DATA_BUF_LEN) {
        uint16_t off_by = next - DATA_BUF_LEN;
        uint16_t num_at_end = 4 - off_by;

        memcpy(pntr, (char*)&val, num_at_end);
        pntr = (uint32_t*) data_buffer.buffer;
        memcpy(pntr, (char*)&val + num_at_end, off_by);
        data_buffer.w_pointer = off_by;
        return 0;
    }
    if(next == DATA_BUF_LEN) {
        next = 0;
    }

    if(next == data_buffer.r_pointer) {
        // BUFFER IS FULL
        return 1;
    }
    
    *pntr = val;
    data_buffer.w_pointer = next;
    return 0;
}

uint32_t pop_from_buffer() {
    // TODO implement me
    return 0;
}

// If some number of words were directly read from the
// data buffer, use this function to appropriately
// update the r_pointer.
// Note this doesn't check that the read_pointer stays
// behind the write_pointer....that's up to the caller

// Also, TODO, check & test this lol
void data_buffer_register_reads(uint16_t n) {
    data_buffer.r_pointer += n;
    // This isn't needed for data length of 2^16, but incase that's ever
    // not the case we'll leave this
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

void data_buffer_register_writes(uint16_t n) {
    data_buffer.w_pointer += n;
    if(data_buffer.w_pointer > DATA_BUF_LEN) {
        data_buffer.w_pointer -= DATA_BUF_LEN;
    }

}

void initialize_data_buffer() {
    data_buffer.r_pointer = 0;
    data_buffer.w_pointer = 0;
}

// TODO test/check this function
uint16_t data_buffer_space_available() {
    if(data_buffer.w_pointer > data_buffer.r_pointer) {
        return DATA_BUF_EFF_LEN - data_buffer.w_pointer + data_buffer.r_pointer;
    }
    return DATA_BUF_EFF_LEN - data_buffer.r_pointer +  data_buffer.w_pointer;
}

// TODO test/check this function
uint16_t data_buffer_contiguous_space_available() {
    if(data_buffer.w_pointer > data_buffer.r_pointer) {
        // There's a kinda special case where if the read pointer is at 0,
        // we cannot write to the last entry, but if the read pointer is elsewhere
        // we can. So the amount of contiguous space depends on if the thing 
        // stopping your is the end of the array, or the read pointer.
        if(data_buffer.r_pointer == 0) {
            return DATA_BUF_EFF_LEN  - data_buffer.w_pointer;
        }
        return (DATA_BUF_LEN - data_buffer.w_pointer);
    }
    return data_buffer.r_pointer - data_buffer.w_pointer - 1;
}
// TODO make these functions stand alone, i.e. not call space_available() functions
// so that we can save a stack frame
uint16_t data_buffer_space_used() {
    return DATA_BUF_EFF_LEN - data_buffer_space_available();
}
uint16_t data_buffer_contiguous_space_used() {
    return DATA_BUF_EFF_LEN - data_buffer_contiguous_space_available();
}
