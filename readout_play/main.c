#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <assert.h>

typedef int tcp_pcb; // 'Forward declaration'

typedef enum TaskID {
    ReadoutData,
    ControlCommand,
    SendData,
    Response,
    Pass
} TaskID;

typedef struct Client {
    tcp_pcb lwip_pcb;
} Client;
Client data_reciever;

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

typedef struct Task {
    TaskID id;
    void* args;
    void* data;
    struct Task* q_next;
    struct Task* pool_next;
    struct Task* pool_prev;
} Task;

#define POOL_DEPTH 1024
typedef struct TaskPool {
    Task* free_list_head;
    Task* active_list_head;
    int num_free;
    int num_active;
} TaskPool;

Task tasks_mem[POOL_DEPTH];
TaskPool task_pool;

void initialize_pool() {
    int i;
    task_pool.free_list_head = tasks_mem;
    task_pool.active_list_head = NULL;
    for(i=0; i < POOL_DEPTH; i++) {
        tasks_mem[i].args = NULL;
        tasks_mem[i].data = NULL;
        tasks_mem[i].id = Pass;
        tasks_mem[i].q_next = NULL;
        tasks_mem[i].pool_next = i+1 < POOL_DEPTH ? tasks_mem+(i+1) : NULL;
        tasks_mem[i].pool_prev = i > 0 ? tasks_mem+(i-1) : NULL;
    }
    task_pool.num_free = POOL_DEPTH;
    task_pool.num_active = 0;
}

Task* task_pool_alloc() {
    // Take the head of the free list and move it to
    // the the active list 
    if(task_pool.num_active >= POOL_DEPTH) {
        // Out of memory, cannot create new tasks
        return NULL;
    }
    // First get a task from the free list
    Task* ret = task_pool.free_list_head;
    // Update the free list such that the top of the list is the next Task after
    // the one that was just retrieved
    task_pool.free_list_head = ret->pool_next;
    task_pool.free_list_head->pool_prev = NULL; // Now that this item is at the top, it has no previous

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
    return ret;
}

void task_pool_free(Task* task) {
    // TODO, should I check that 'task' is in the "active" list and not in
    // the free list? or something else?
    // remove task from active_list
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
    free_head->pool_prev = task;
    task->pool_next = free_head;
    task->pool_prev = NULL;
    task_pool.free_list_head = task;

    task_pool.num_active -= 1;
    task_pool.num_free += 1;
}

// TODO, should have a different Queue for each type
// of task, i.e. a readout queue a response queue etc.
// Then I can have a priority based decision that accounts for how
// many items are in each queue.
typedef struct TaskQueue {
    Task* head;
    Task* tail;
    int length;
} TaskQueue;
TaskQueue task_queue;

void initialize_queue() {
    task_queue.head = NULL;
    task_queue.tail = NULL;
    task_queue.length = 0;
}

void push_to_queue(Task* new){
    Task* last;
    last = task_queue.tail;
    if(last) {
        last->q_next = new;
    } else {
        task_queue.head = new;
    }
    task_queue.tail = new;
    new->q_next = NULL;
    task_queue.length += 1;
}

Task* pop_from_queue() {
    if(task_queue.length == 0) {
        return NULL;
    }
    Task* item = task_queue.head;
    task_queue.head = item->q_next;
    task_queue.length -= 1;

    item->q_next = NULL;
    return item;
}


void interrupt_handler(int mask) {

}

int execute_task(TaskID todo_id) {
    switch(todo_id) {
        case ReadoutData:
            break;
        case ControlCommand:
            break;
        case SendData:
            break;
        case Response:
            break;
        case Pass:
            break;
        default:
            break;
    }
    return 0;
}

void initialize_test() {
    initialize_pool();
    initialize_queue();
    assert(task_pool.num_active == 0);
    assert(task_pool.num_free == POOL_DEPTH);
    assert(task_pool.active_list_head == NULL);
    assert(task_pool.free_list_head != NULL);
    assert(task_queue.head == NULL);
    assert(task_queue.tail == NULL);
    assert(task_queue.length == 0);
}

void alloc_one_dealloc_one_test() {
    Task* task = task_pool_alloc();
    assert(task_pool.num_active == 1);
    assert(task_pool.num_free == POOL_DEPTH-1);
    assert(task_pool.active_list_head==task);
    assert(task_queue.head == NULL);
    assert(task_queue.tail == NULL);
    assert(task_queue.length == 0);
    task->id = ReadoutData;
    push_to_queue(task);

    Task* popped = pop_from_queue();
    assert(popped == task);
    task_pool_free(task);

    assert(task_pool.num_active ==0);
    assert(task_pool.num_free ==POOL_DEPTH);
    assert(task_pool.active_list_head == NULL);
}

void assert_n_active(const int n) {
    assert(task_pool.num_active ==n);
    assert(task_pool.num_free ==POOL_DEPTH - n);
    if(n == 0) {
        assert(task_pool.active_list_head == NULL);
        assert(task_pool.free_list_head != NULL);
    } else if(n == POOL_DEPTH) {
        assert(task_pool.free_list_head == NULL);
        assert(task_pool.active_list_head != NULL);
    } else {
        assert(task_pool.free_list_head != NULL);
        assert(task_pool.active_list_head != NULL);
    }

}

void assert_n_queued(const int n) {
    assert(task_queue.length == n);
    if(n == 0) {
        assert(task_queue.head == NULL);
        assert(task_queue.tail == NULL);
    } else if(n == 1) {
        assert(task_queue.head != NULL);
        assert(task_queue.head == task_queue.tail);
    } else {
        assert(task_queue.head != task_queue.tail);
        assert(task_queue.tail != NULL);
        assert(task_queue.head != NULL);
    }
}


void alloc_n_dealloc_n(const int n) {
    int i;
    assert_n_active(0);
    assert_n_queued(0);
    for(i=0; i<n; i++) {
        Task* task = task_pool_alloc();
        assert_n_active(i+1);
        assert_n_queued(i);
        push_to_queue(task);
        assert_n_queued(i+1);
    }
    for(i=n; i>=0; i++) {
        assert_n_active(i);
        assert_n_queued(i);
        Task* t = pop_from_queue();

        assert_n_active(i);
        assert_n_queued(i-1);
        task_pool_free(t);
        assert_n_active(i-1);
        assert_n_queued(i-1);
    }

}

int main(int argc, char** argv) {
    const int run = 1;

    initialize_test();
    alloc_one_dealloc_one_test();

    alloc_n_dealloc_n(10);
    return 0;
}
