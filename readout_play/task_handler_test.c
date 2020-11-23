#include <sys/time.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>

#include "data_buffer_v2.h"
#include "task_handler.h"
#include "queue.h"
#include "fake_lwip.h"
static const char thumbs_up[5] = {0xF0, 0x9F, 0x91, 0x8d, '\0'};
static const char check[4] = {0xE2, 0x9C, 0x85, '\0'};
pid_t CHILD = -1;

// In case we fail an assertion while having open FDs and children
void abort_exit(int n) {
    clean_up_fds();

    // If child: kill child!
    if(CHILD) {
        kill(CHILD, SIGTERM);
    }
    exit(1);
}

static pid_t launch_reciever() {
    CHILD = fork();
   if(CHILD == 0) {
       // If here I AM the child process
       char* args[2] = {"./fifo_recvr", NULL};
       execvp(args[0], args);
       exit(0);
   }
   return CHILD;
}

static void test_zero_tasks() {
    Task* decided_task;

    printf("Starting zero tasks tests\n");
    initialize_pool();
    initialize_task_list();
    initialize_data_buffer();
    //initialize_lwip();

    assert(task_list.send_queue.length == 0);
    assert(task_list.trigger_readout_queue.length == 0);
    assert(task_list.data_readout_queue.length == 0);

    decided_task = decide_on_task();
    assert(decided_task == NULL);
    assert(task_list.send_queue.length == 0);
    assert(task_list.trigger_readout_queue.length == 0);
    assert(task_list.data_readout_queue.length == 0);
    printf("Passed zero tasks tests\n\n");
}

static void test_single_task() {
    printf("Starting single tasks tests\n");
    initialize_pool();
    initialize_task_list();
    initialize_data_buffer();
    //initialize_lwip();

    Task* task = task_pool_alloc();
    assert(task);
    task->id = READOUT_TRIGGER;
    push_to_queue(&task_list.trigger_readout_queue, task);

    Task* decided_task = decide_on_task();
    assert(decided_task == task);
    assert(task_list.send_queue.length == 0);
    assert(task_list.trigger_readout_queue.length == 0);
    assert(task_list.data_readout_queue.length == 0);


    task_pool_free(task);
    printf("Passed single tasks tests\n\n");
}

static void test_three_tasks() {
    Task* decided_task1;
    Task* decided_task2;
    Task* decided_task3;

    printf("Starting three tasks tests\n");

    initialize_pool();
    initialize_task_list();
    initialize_data_buffer();
    //initialize_lwip();

    Task* task1 = task_pool_alloc();
    Task* task2 = task_pool_alloc();
    Task* task3 = task_pool_alloc();
    assert(task1);
    assert(task2);
    assert(task3);
    task1->id = SEND_DATA;
    task2->id = READOUT_DATA;
    task3->id = READOUT_TRIGGER;
    push_to_queue(&task_list.send_queue, task1);
    push_to_queue(&task_list.data_readout_queue, task2);
    push_to_queue(&task_list.trigger_readout_queue, task3);

    assert(task_list.send_queue.length == 1);
    assert(task_list.data_readout_queue.length == 1);
    assert(task_list.trigger_readout_queue.length == 1);

    decided_task1 = decide_on_task();
    assert(decided_task1 == task1);
    assert(task_list.send_queue.length == 0);
    assert(task_list.data_readout_queue.length == 1);
    assert(task_list.trigger_readout_queue.length == 1);

    decided_task2 = decide_on_task();
    assert(decided_task2 == task2);
    assert(task_list.send_queue.length == 0);
    assert(task_list.data_readout_queue.length == 0);
    assert(task_list.trigger_readout_queue.length == 1);

    decided_task3 = decide_on_task();
    assert(decided_task3 == task3);
    assert(task_list.send_queue.length == 0);
    assert(task_list.data_readout_queue.length == 0);
    assert(task_list.trigger_readout_queue.length == 0);

    task_pool_free(task1);
    task_pool_free(task2);
    task_pool_free(task3);

    printf("Passed single tasks tests\n\n");
}

static void test_readout_trigger() {

    printf("Starting readout trigger test\n");
    initialize_pool();
    initialize_task_list();
    initialize_data_buffer();
    data_client = (struct tcp_pcb*) 1; //BS to make trigger readout task not complain

    Task* task = task_pool_alloc();

    task->id = READOUT_TRIGGER;
    push_to_queue(&task_list.trigger_readout_queue, task);

    main_loop();

    assert(task_list.trigger_readout_queue.length == 0);
    assert(task_list.data_readout_queue.length == 1);
    assert(data_buffer_space_used() == 16);

    task_pool_free(task);

    printf("Passed readout trigger test\n\n");
}

static void test_send_while_full() {

    printf("Starting send while full test\n");
    initialize_pool();
    initialize_task_list();
    initialize_data_buffer();
    data_client = (struct tcp_pcb*) 1; //BS to make trigger readout task not complain

    pid_t child_pid = launch_reciever();
    usleep(100000); // 100ms sleep to make sure the recvr has time to launch
    initialize_lwip(); // Opens unix FIFOs to child recvr process

    signal(SIGABRT, abort_exit);
    // First fill the buffer
    data_buffer_register_writes(DATA_BUF_LEN);

    // Now queue up a send data task
    Task* task = task_pool_alloc();
    task->id = SEND_DATA;
    push_to_queue(&task_list.send_queue, task);

    // Proces the task
    main_loop();

    assert(data_buffer_contiguous_space_used() < DATA_BUF_LEN);

    // Kill child!!!! (TODO, Should I check that child_pid is still running?)
    kill(child_pid, SIGTERM);
    clean_up_fds();
    signal(SIGABRT, NULL);

    printf("Passed send while full test\n\n");

}
int main() {

    test_zero_tasks();
    test_single_task();
    test_three_tasks();

    test_readout_trigger();
    test_send_while_full();
    printf("All tests passed %s %s\n", check, thumbs_up);

    return 0;
}
