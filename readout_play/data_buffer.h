/*
 * data_buffer.h
 *
 *  Created on: Nov 19, 2019
 *      Author: marzece
 */

#ifndef SRC_DATA_BUFFER_H_
#define SRC_DATA_BUFFER_H_

#include <stdio.h>
#include <stdint.h>

#define DATA_BUF_LEN 65536
// The effective length is one less than the max so that
// a full buffer is different from an empty one.
#define DATA_BUF_EFF_LEN (DATA_BUF_LEN - 1)

// Data buffer is a ring buffer for holding data that will
// be sent to the registered data_receiver
typedef struct DataBuffer {
    char buffer[DATA_BUF_LEN];
    uint16_t r_pointer;
    uint16_t w_pointer;
} DataBuffer;
DataBuffer data_buffer;

void initialize_data_buffer();
uint16_t push_to_buffer(uint32_t val);
uint32_t data_buffer_space_available();
uint32_t data_buffer_contiguous_space_available();
uint32_t data_buffer_contiguous_space_used();
uint32_t data_buffer_space_used();
void data_buffer_register_reads(uint16_t n);
void data_buffer_register_writes(uint16_t n);

#endif /* SRC_DATA_BUFFER_H_ */
