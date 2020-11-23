#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "queue.h"

static const char thumbs_up[5] = {0xF0, 0x9F, 0x91, 0x8d, '\0'};
static const char check[4] = {0xE2, 0x9C, 0x85, '\0'};

TaskQueue initialize() {
    TaskQueue q;
    initialize_queue(&q);
    initialize_pool();
    return q;
}

static int q_count(Task* head) {
    int count = 0;
    while(head) {
        count +=1;
        head = head->q_next;
    }
    return count;
}

static int check_valid(TaskQueue q) {
    Task* head = q.head;
    int ret = 1;
    while(head){
        ret = ret && head->queued;
        head = head->q_next;
    }
    return ret;

}

static void test_initialize() {
    printf("Starting Initialization test\n");
    TaskQueue q_ut = initialize();

    assert(q_ut.head == NULL);
    assert(q_ut.tail == NULL);
    assert(q_ut.length == 0);
    assert(q_count(q_ut.head) == 0);

    printf("Passed Initialization test\n\n");
}

static void test_single_task() {
    TaskQueue q_ut;
    Task* task;
    Task* task2;

    printf("Starting Single task test\n");

    q_ut = initialize();
    
    task = task_pool_alloc();
    push_to_queue(&q_ut, task);

    assert(q_ut.head == task);
    assert(q_ut.tail == task);
    assert(q_ut.length == 1);
    assert(check_valid(q_ut));
    assert(task->q_next == NULL);

    task2 = pop_from_queue(&q_ut);
    assert(task == task2);
    assert(task->q_next == NULL);
    assert(q_ut.head == NULL);
    assert(q_ut.tail == NULL);
    assert(q_ut.length == 0);

    task_pool_free(task);
    printf("Passed Single task test\n\n");
}

static void test_two_tasks() {
    TaskQueue q_ut;
    Task* task1;
    Task* task2;

    Task* popd1;
    Task* popd2;

    printf("Starting Two task test\n");
    q_ut = initialize();

    task1 = task_pool_alloc();
    task2 = task_pool_alloc();

    // Task2 ---> Task1
    push_to_queue(&q_ut, task2);
    push_to_queue(&q_ut, task1);

    assert(q_ut.head == task2);
    assert(q_ut.tail == task1);
    assert(q_ut.length == 2);
    assert(task2->q_next == task1);
    assert(task1->q_next == NULL);
    assert(check_valid(q_ut));

    popd2 = pop_from_queue(&q_ut);

    assert(q_ut.head == task1);
    assert(q_ut.tail == task1);
    assert(q_ut.length == 1);
    assert(task2->q_next == NULL);
    assert(task1->q_next == NULL);
    assert(popd2 == task2);
    assert(check_valid(q_ut));

    popd1 = pop_from_queue(&q_ut);
    assert(q_ut.head == NULL);
    assert(q_ut.tail == NULL);
    assert(q_ut.length == 0);
    assert(task2->q_next == NULL);
    assert(task1->q_next == NULL);
    assert(popd1 == task1);
    assert(check_valid(q_ut));


    task_pool_free(task1);
    task_pool_free(task2);
    printf("Passed Two task test\n\n");
}

static void test_full_queue() {
    TaskQueue q_ut;
    int i;
    Task* tasks[POOL_DEPTH];
    Task* popd_tasks[POOL_DEPTH];

    printf("Starting Full queue test\n");

    q_ut = initialize();
    memset(tasks, 0, sizeof(Task*)*POOL_DEPTH);
    memset(popd_tasks, 0, sizeof(Task*)*POOL_DEPTH);

    for(i=0; i<POOL_DEPTH; i++) {
        tasks[i] = task_pool_alloc();
    }
    for(i=0; i<POOL_DEPTH; i++) {
        push_to_queue(&q_ut, tasks[i]);
        assert(q_count(q_ut.head) == i+1);
        assert(q_ut.length == i+1);
        assert(q_ut.tail == tasks[i]);
        assert(check_valid(q_ut));
        if(i) {
            assert(tasks[i-1]->q_next == tasks[i]);
        }
    }
    
    // Now pop all the tasks
    for(i=0; i<POOL_DEPTH; i++) {
        popd_tasks[i] = pop_from_queue(&q_ut);
        assert(q_count(q_ut.head) == POOL_DEPTH - (i+1));
        assert(q_ut.length == POOL_DEPTH - (i+1));
        assert(popd_tasks[i]->q_next == NULL);
        assert(check_valid(q_ut));
        assert(popd_tasks[i] == tasks[i]);
        if(i != POOL_DEPTH - 1) {
            assert(q_ut.head == tasks[i+1]);
            assert(q_ut.tail == tasks[POOL_DEPTH-1]);
        }
    }

    // Make sure it's empty
    assert(q_ut.head == NULL);
    assert(q_ut.tail == NULL);
    assert(q_ut.length == 0);

    printf("Passed Full queue test\n\n");
}

static void test_pop_from_empty() {
    TaskQueue q_ut;
    Task* task;

    printf("Starting pop from empty test\n");
    q_ut = initialize();

    task = pop_from_queue(&q_ut);
    assert(task == NULL);
    assert(q_ut.length == 0);
    assert(q_ut.head == NULL);
    assert(q_ut.tail == NULL);

    printf("Passed pop from empty test\n\n");
}

static void test_two_queues() {
    TaskQueue q1, q2;
    int i;
    const int N_ITEMS = 100;
    Task* task;

    printf("Starting two queue test\n");
    initialize_pool();
    initialize_queue(&q1);
    initialize_queue(&q2);

    // First fill the queues
    int expected_q1_length = 0;
    int expected_q2_length = 0;
    for(i=0; i< N_ITEMS; i++) {
        task = task_pool_alloc();
        if(i%2) {
            expected_q1_length += 1;
            push_to_queue(&q1, task);
            assert(q1.tail == task);
        } else {
            expected_q2_length += 1;
            push_to_queue(&q2, task);
            assert(q2.tail == task);
        }

        assert(check_valid(q1));
        assert(check_valid(q2));
        assert(q1.length == expected_q1_length);
        assert(q2.length == expected_q2_length);
        assert(q_count(q1.head) == q1.length);
        assert(q_count(q2.head) == q2.length);
    }

    // Now move pop from one queue and move then to the other queue
    while(q2.length > 0) {
        task = pop_from_queue(&q2);
        push_to_queue(&q1, task);
        expected_q1_length +=1;
        expected_q2_length -=1;
        assert(q1.length == expected_q1_length);
        assert(q2.length == expected_q2_length);
        assert(q_count(q1.head) == q1.length);
        assert(q_count(q2.head) == q2.length);
        assert(check_valid(q1));
        assert(check_valid(q2));
    }
    // Now pop the rest from q1
    while(q1.length) {
        task = pop_from_queue(&q1);
        expected_q1_length -=1;
        assert(q1.length == expected_q1_length);
        assert(q_count(q1.head) == q1.length);
        assert(check_valid(q1));
    }
    printf("Passed two queue test\n\n");
}

static void test_bad_push() {
    TaskQueue q_ut;
    Task* task;
    printf("Starting bad push test\n");
    q_ut = initialize();

    task = NULL;
    push_to_queue(&q_ut, task);
    assert(q_ut.head == NULL);
    assert(q_ut.tail == NULL);
    assert(q_ut.length == 0);
    assert(q_count(q_ut.head) == 0);


    printf("Passed bad push test\n\n");
}

int main() {
    test_initialize();
    test_single_task();
    test_two_tasks();
    test_full_queue();

    test_pop_from_empty();
    test_two_queues();

    test_bad_push();
    printf("All tests passed %s %s\n", check, thumbs_up);

    return 0;
}
