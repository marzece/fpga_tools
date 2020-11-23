/*
 * queue.h
 *
 *  Created on: Nov 14, 2019
 *      Author: marzece
 */
// The goal here is to have two data structures.
// One is an ordered queue to keep track of what the server should be doing.
// Tasks will be added to this queue to make sure data is readout and client
// interactions are handled.
// The other structure is a memory pool. Since the MicroBlaze does not
// support dynamic allocation all memory for tasks must be allocated statically.
// The memory pool's goal is to keep track of which tasks have been given out.
//
// The memory pool consists of two linked lists; one list of free Tasks, one
// list of 'in use' Tasks. When a task is allocated it is removed the free
// list and added to the 'in use' list. When a task is freed the reverse process
// happens.


#ifndef QUEUE_H_
#define QUEUE_H_
#include <stdint.h>

#define POOL_DEPTH 1024


typedef enum TaskID {
    READOUT_TRIGGER,
    READOUT_DATA,
    SEND_DATA,
    CONTROLCOMMAND,
    RESPONSE,
} TaskID;

typedef struct DataReadoutArgs {
    uint8_t remaining_channels;
    uint16_t remaining_samples;
    uint32_t trigger_length;

} DataReadoutArgs;

typedef struct TriggerReadoutArgs {
uint8_t nop;
} TriggerReadoutArgs;

typedef struct ControlArgs {
    int nop;
} ControlArgs;

typedef struct SendDataArgs {
    uint32_t size_to_send;
} SendDataArgs;

typedef struct TaskArgs {
    union {
        DataReadoutArgs data_readout_args;
        TriggerReadoutArgs trigger_readout_args;
        ControlArgs control_args;
        SendDataArgs send_data_args;
    };
} TaskArgs;

typedef struct Task {
    TaskID id;
    TaskArgs args;
    int active;
    int queued;
    struct Task* q_next;
    struct Task* pool_next;
    struct Task* pool_prev;
} Task;

typedef struct TaskPool {
    Task* free_list_head;
    Task* active_list_head;
    int num_free;
    int num_active;
} TaskPool;

typedef struct TaskQueue {
    Task* head;
    Task* tail;
    int length;
} TaskQueue;

// TODO make this into a real priority queue
typedef struct TaskManager {
    TaskQueue trigger_readout_queue;
    TaskQueue data_readout_queue;
    TaskQueue send_queue;
    TaskQueue control_queue;
} TaskManager;


Task tasks_mem[POOL_DEPTH];
TaskPool task_pool;
TaskManager task_list;
int semaphore;
void initialize_pool();
void initialize_task_list();
Task* task_pool_alloc();
void task_pool_free(Task* task);
void initialize_queue(TaskQueue* task_queue);
void push_to_queue(TaskQueue* task_queue, Task* new_task);
Task* pop_from_queue(TaskQueue* task_queue);
int queue_length(TaskQueue* task_queue);
int pool_avail();
int pool_used();
#endif /* QUEUE_H_ */
