/*
 * task_handler.c
 *
 *  Created on: Nov 19, 2019
 *      Author: marzece
 */
#include <stdint.h>
#include "data_buffer_v2.h"
#include "task_handler.h"

struct tcp_pcb* data_client = NULL;

#define TRIGGER_FIFO_ADDR  0xFFFF0020
static uint32_t word = 0x0000B00F;
uint32_t Xil_In32(addr) {
    static int i = 0;
    int ret = -1;
    if(addr == TRIGGER_FIFO_ADDR) {
        if(i%4 == 0) {
            // trigger word
            ret = word++;
        } else if(i%4 == 1) {
            // trigger_length
            ret = 100;
        } else if(i%4 == 2) {
            // clock 1
            ret = 0x12345678;
        } else {
            // clock 1
            ret = 0xbc;
        }
        i+=1;
    }
    return ret;
}

Task* decide_on_task() {
    if(task_list.send_queue.length > 0) {
        return pop_from_queue(&task_list.send_queue);
    }
    if(task_list.data_readout_queue.length > 0) {
        return pop_from_queue(&task_list.data_readout_queue);
    }
    if(task_list.trigger_readout_queue.length > 0) {
        return pop_from_queue(&task_list.trigger_readout_queue);
    }
    return NULL;

}

int perform_task(Task* task) {
    switch(task->id) {
    case READOUT_TRIGGER:
        return readout_trigger(task);
    case READOUT_DATA:
        return readout_data(task);
    case SEND_DATA:
        return send_data(task);
    case CONTROLCOMMAND:
        return 0;
    case RESPONSE:
        return 0;
    default:
        return 0;
    }
}

void main_loop() {
    Task* task = NULL;
    task = decide_on_task();
    if(task) {
        perform_task(task);
        task_pool_free(task);
    }
}

int readout_trigger(Task* task) {
    // (I think) The goal here should just be to readout the trigger fifo
    // and then setup a "READOUT_DATA" task with the number of samples
    // that need to be readout.
    if(!data_client) {
        printf("Event recieved but there's no receiver\n");
        return 1;
    }


    // See PG080 of Xilinx Documentation for AXI-Stream FIFO for origins of these numbers
    const uint32_t trigger_fifo_addr = TRIGGER_FIFO_ADDR;
    if(data_buffer_space_available() < 4*sizeof(uint32_t)) {
        // eeep...not sure what to do here. Should flag
        // an error I think....TODO
        printf("OOM error ocurred in readout trigger\n");
        return 1;
    }
    uint32_t trigger_word = Xil_In32(trigger_fifo_addr);
    uint32_t length = Xil_In32(trigger_fifo_addr);
    uint32_t clock1 = Xil_In32(trigger_fifo_addr);
    uint32_t clock2 = Xil_In32(trigger_fifo_addr);
    if(length == 0) {
        printf("Zero length trigger found....this seems like an error. Throwing it out\n");
        return 1;
    }

    // Shouldn't need to check the return value of these pushes
    // b/c I already made sure enough space existed
    push_to_buffer(trigger_word);
    push_to_buffer(length);
    push_to_buffer(clock1);
    push_to_buffer(clock2);

    Task* new_task = task_pool_alloc();
    new_task->id = READOUT_DATA;
    new_task->args.data_readout_args.remaining_channels = 16;
    new_task->args.data_readout_args.remaining_samples = length;
    new_task->args.data_readout_args.trigger_length = length;

    push_to_queue(&task_list.data_readout_queue, new_task);
    return 0;
}

static inline int lookup_fifo_addr(int which_fifo, uint32_t* base_addr, uint32_t* high_addr) {
    static uint32_t base_addrs[17] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                     13, 14, 15, 15};

    if(which_fifo < 0 || which_fifo > 16) {
        *base_addr = 0;
        *high_addr = 0;
        return -1;
    }
    *base_addr = base_addrs[which_fifo];
    *high_addr = base_addrs[which_fifo] + 1;
    return 0;
}

uint16_t channel_num_to_fifo_num(uint16_t channel) {
    // Each channel has a FIFO but only channel 0 has FIFO 0.
    // Every other channel (n) has FIFO n+1, e.g. channel 4 has FIFO 5.
    return channel > 0 ? channel+1 : channel;
}

uint32_t channel_num_to_fifo_addr(uint16_t channel) {
    // Each channel has a FIFO but only channel 0 has FIFO 0.
    // Every other channel (n) has FIFO n+1, e.g. channel 4 has FIFO 5.
    int fifo_num = channel > 0 ? channel+1 : channel;
    uint32_t base, high;
    lookup_fifo_addr(fifo_num, &base, &high);
    return base + 0x20;
}

uint32_t fake_sample(int hit) {
    const int n_its = 50;
    int i;
    uint16_t v1=0;
    uint16_t v2=0;
    for(i=0; i < n_its; i++) {
        v1 += rand() % 100;
        v2 += rand() % 100;
    }
    v1 /= (float)n_its;
    v2 /= (float)n_its;
    if(hit) {
        v1 *= 2;
        v2 *= 2;
    }
    v1 += 8192;
    v2 += 8192;

    return (v1 << 16) | v2;
}

int readout_data(Task* task) {

    DataReadoutArgs args = task->args.data_readout_args;
    uint16_t i=0;
    uint16_t contiguous_reaout = data_buffer_contiguous_space_available()/sizeof(uint32_t); // TODO ???

    uint16_t total_readout = data_buffer_space_available()/sizeof(uint32_t);

    uint16_t readout_diff = total_readout - contiguous_reaout;

    // volatile uint32_t* current_fifo_addr (uint32_t *)(channel_num_to_fifo_addr(16 - args.remaining_channels));
    uint32_t* pntr = (uint32_t*)(data_buffer.buffer+data_buffer.w_pointer);

     // Hot loop
    while((args.remaining_channels > 0 && args.remaining_samples > 0) && (i < contiguous_reaout) ) {

        //*pntr = args.remaining_channels;
       *pntr = fake_sample(args.remaining_channels==1 && args.remaining_samples > 25 && args.remaining_samples < 75);

        pntr = pntr+1;
        i += 1;
        args.remaining_samples--;

        if(args.remaining_samples == 0 && args.remaining_channels > 0) {
            args.remaining_channels -= 1;
            //current_fifo_add =.....;
            args.remaining_samples = args.trigger_length;
        }
    }

    data_buffer_register_writes(i*sizeof(uint32_t));
    if(readout_diff > 0  && args.remaining_samples > 0 && args.remaining_channels > 0) {
        push_to_buffer(fake_sample(args.remaining_channels==1 && args.remaining_samples > 25 && args.remaining_samples < 75));
        i+=1;
        args.remaining_samples--;

        if(args.remaining_samples == 0 && args.remaining_channels > 0) {
            args.remaining_channels = args.remaining_channels -1;
            args.remaining_samples = args.trigger_length;
        }
    }

    if(data_buffer_space_used()) {
        // We've added data to the sending buffer
        // Queue up a task ot send that data
        Task* send_task = task_pool_alloc();
        if(!send_task) {
            // fuck fuck fuck task pool is empty
            return 1;
        }
        send_task->id = SEND_DATA;
        push_to_queue(&task_list.send_queue, send_task);
    }
    if(args.remaining_samples > 0 && args.remaining_channels > 0) {
        // Queue up new readout task
        Task* readout_task = task_pool_alloc();
        if(!readout_task) {
            // fuck fuck fuck
            return 1;
        }
        readout_task->id = READOUT_DATA;
        readout_task->args.data_readout_args = args;
        push_to_queue(&task_list.data_readout_queue, readout_task);
    }
    return 0;
}

int send_data(Task* task) {
    err_t err;
    
    if(!data_client) {
        printf("DATA Client not registered, cannot send data");
        return 1;
    }

    uint32_t contiguous_to_send = data_buffer_contiguous_space_used();
    uint32_t total_to_send = data_buffer_contiguous_space_used();

    // Do i need to check snd_buf size if I don't copy?
    // Should I try and avoid using a divide here? (very costly operation)
    // printf("%i %i\n", contiguous_to_send, total_to_send);
    // I think just doing '>>2' should work just as wel
    uint16_t sendable = tcp_sndbuf(data_client);

    // Need to determine how much data to actually send
    // If the amount of contiguous memory that needs to be sent
    // is greater than (or equal) to the amount can be sent we should
    // as much as possible.
    // If the total_to_send is greater (or equal) to the sendable amount
    // but the contiguous amount is not we should send the first contiguous chunk
    // then send as of the  second contiguous chunk as possible
    // If the contiguous or total is less than the sendable amount we should
    // send the first contiguous chunk then (if necessary) send the second
    // contiguous chunk.
    uint32_t chunk1_size;
    uint32_t chunk2_size;
    if(total_to_send == contiguous_to_send || contiguous_to_send <= sendable) {
        chunk1_size = contiguous_to_send > sendable ? sendable: contiguous_to_send;
        chunk2_size = 0;
    } else {
        // If here then chunk2 has data AND there exists enough space in the send buf to send some
        chunk1_size = contiguous_to_send;
        chunk2_size = total_to_send - contiguous_to_send;
        uint16_t snd_buf_remaining = (sendable-chunk1_size);
        chunk2_size = chunk2_size > snd_buf_remaining ? snd_buf_remaining : chunk2_size;
    }


    // NOTE I am ignoring network byte ordering.
    // The MicroBlaze is little endian so the byte order on the wire will be wrong.
    // It's my wire I can do what I want. Clients beware.
    err = tcp_write(data_client, data_buffer.buffer+data_buffer.r_pointer, chunk1_size, 0);
    if(err != ERR_OK) {
        goto ERROR;
    }
    if(chunk2_size > 0) {
        err = tcp_write(data_client, data_buffer.buffer+data_buffer.r_pointer, chunk2_size, 0);
        if(err != ERR_OK) {
            goto ERROR;
        }
    }


    tcp_output(data_client);
    // tcp_sent(data_client, send_ack);
    // task->args.send_data_args.size_to_send = chunk1_size + chunk2_size;
    // tcp_arg(data_client, task);
    data_buffer_register_reads(chunk1_size + chunk2_size);
    return ERR_OK;

    ERROR:
        // uhhhhh what do??...should queue a re-send probably
        // that brings up the question... who receives any error I throw here?
        // What do they do with it?
        printf("err in data sender, %i\n", err);
        //tcp_sent(data_client, NULL);
        return ERR_OK;

}

err_t send_ack(void* arg, struct tcp_pcb* client, u16_t len) {
    // Assuming (for now) that all packets are ack'd in order they are sent
    // Move the read_pointer for the data_buffer by the number of 4-byte (32bit) words
    // that were written (what if the amount sent isn't a 4-byte thing??????????? TODO...look into it)

    uint16_t sendable = tcp_sndbuf(data_client);
    if(data_buffer_space_used() > sendable && sendable > 4096) {
        Task* send_task = task_pool_alloc();
        if(!send_task) {
            return ERR_OK;
        }
        send_task->id = SEND_DATA;
        push_to_queue(&task_list.send_queue, send_task);
    }
    return ERR_OK;
}
