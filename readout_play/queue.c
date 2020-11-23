#include "queue.h"

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <assert.h>

void initialize_pool() {
    int i;
    task_pool.free_list_head = tasks_mem;
    task_pool.active_list_head = NULL;
    for(i=0; i < POOL_DEPTH; i++) {
        tasks_mem[i].id = -1;
        tasks_mem[i].active = 0;
        tasks_mem[i].queued = 0;
        tasks_mem[i].q_next = NULL;
        tasks_mem[i].pool_next = (i+1) < POOL_DEPTH ? &(tasks_mem[i+1]) : NULL;
        tasks_mem[i].pool_prev = i > 0 ? &(tasks_mem[i-1]) : NULL;
    }
    task_pool.num_free = POOL_DEPTH;
    task_pool.num_active = 0;
}

void initialize_task_list() {
    initialize_queue(&task_list.control_queue);
    initialize_queue(&task_list.send_queue);
    initialize_queue(&task_list.data_readout_queue);
    initialize_queue(&task_list.trigger_readout_queue);
}

Task* task_pool_alloc() {
    // Take the head of the free list and move it to
    // the the active list
    if(task_pool.num_active >= POOL_DEPTH) {
        // Out of memory, cannot create new tasks
        return NULL;
    }
    semaphore = 4;

    // First get a task from the free list
    Task* ret = task_pool.free_list_head;
    // Update the free list such that the top of the list is the next Task after
    // the one that was just retrieved
    task_pool.free_list_head = ret->pool_next;
    if(task_pool.free_list_head) {
        task_pool.free_list_head->pool_prev = NULL; // Now that this item is at the top, it has no previous
    }

    // Add the Task that was just retrieved to the active task list.
    // Here we choose to add it at the top, which means the previous top item
    // needs to be updated so that its previous item is the new guy.
    Task* active_head = task_pool.active_list_head;
    if(active_head) {
        active_head->pool_prev = ret;
        ret->pool_next = active_head;
    } else {
        ret->pool_next = NULL;
    }
    task_pool.active_list_head = ret;
    ret->pool_prev = NULL;

    task_pool.num_active +=1;
    task_pool.num_free -= 1;

    ret->active = 1;
    semaphore = 0;
    return ret;
}


void task_pool_free(Task* task) {
    // TODO, should I check that 'task' is in the "active" list and not in
    // the free list? or something else?
    // remove task from active_list
    if(!task || task_pool.num_active == 0) {
        return;
    }
    if(!task->active) {
        return;
    }
    semaphore = 1;

    if(task_pool.num_active > 1) {
        Task* next = task->pool_next;
        Task* prev = task->pool_prev;
        // This was the only allocated task
        // nop
        if(!next) {
            // The task to be removed is the tail of the list (it has no next)
            prev->pool_next = NULL;
        } else if (!prev) {
            // The task to be removed is the head of the list (it has no prev)
            next->pool_prev = NULL;
        } else {
            // The task to be removed is in the middle of the list (it has a prev and next)
            next->pool_prev = prev;
            prev->pool_next = next;
        }
    }

    // Need to update active_head the task to be freed is the active_head
    if(task == task_pool.active_list_head) {
        task_pool.active_list_head = task->pool_next;
    }


    // Add it to free_list (just stick it on the head)
    Task* free_head = task_pool.free_list_head;
    if(free_head) {
        free_head->pool_prev = task;
    }
    task->pool_next = free_head;
    task->pool_prev = NULL;
    task_pool.free_list_head = task;

    task_pool.num_active -= 1;
    task_pool.num_free += 1;

    task->active =0;
    semaphore = 0;
}

void initialize_queue(TaskQueue* task_queue) {
    task_queue->head = NULL;
    task_queue->tail = NULL;
    task_queue->length = 0;
}

void push_to_queue(TaskQueue* task_queue, Task* new_task) {
    if(!new_task ) {
        printf("ohhhh nooooooo\n");
        return;
    }
    if(new_task->queued) {
        printf("Attempting to push already queued task to queue\n");
        return;
    }

    Task* last;
    semaphore = 2;
    last = task_queue->tail;
    if(last) {
        last->q_next = new_task;
    } else {
        task_queue->head = new_task;
    }
    task_queue->tail = new_task;
    new_task->q_next = NULL;
    task_queue->length += 1;
    new_task->queued = 1;

    // For debugging only, remove this later
    if(task_queue->length > 0 && task_queue->head == NULL) {
        printf("error found\n");
        task_queue->head = new_task;
    }
    semaphore =0;
}

Task* pop_from_queue(TaskQueue* task_queue) {
    if(task_queue->length == 0) {
        return NULL;
    }
    semaphore = 3;
    Task* item = task_queue->head;
    task_queue->head = item->q_next;
    task_queue->length -= 1;
    if(task_queue->length == 0) {
        task_queue->tail = NULL;
    }
    item->q_next = NULL;

    // For debugging only, remove this later
    if(task_queue->length > 0 && task_queue->head == NULL) {
        printf("error found\n");
        task_queue->head = NULL;
    }
    item->queued = 0;
    semaphore = 0;
    return item;
}

int queue_length(TaskQueue* task_queue) {
    if(task_queue) {
        return task_queue->length;
    }
    return task_list.control_queue.length + task_list.data_readout_queue.length+task_list.send_queue.length + task_list.trigger_readout_queue.length;

}

int pool_avail() {
    return task_pool.num_free;
}

int pool_used() {
    return task_pool.num_active;
}
