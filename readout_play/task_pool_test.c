#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>
#include <assert.h>
#include "queue.h"

static const char thumbs_up[5] = {0xF0, 0x9F, 0x91, 0x8d, '\0'};
static const char check[4] = {0xE2, 0x9C, 0x85, '\0'};

static void print_task_pool() {
    printf("Free head = %p\n", task_pool.free_list_head);
    printf("num free = %i\n", task_pool.num_free);
    printf("Active head = %p\n", task_pool.active_list_head);
    printf("num active = %i\n", task_pool.num_active);

}
static void print_tasks() {
    int i=0;
    for(i=0; i < POOL_DEPTH; i++) {
        printf("index = %i, me = %p , next = %p prev = %p\n",
               i, &(tasks_mem[i]), tasks_mem[i].pool_next, tasks_mem[i].pool_prev);
    }
}

/* Arrange the N elements of ARRAY in random order.
   Only effective if N is much smaller than RAND_MAX;
   if this may not be the case, use a better random
   number generator. 
   Copy/pasted from
   https://stackoverflow.com/questions/6127503/shuffle-array-in-c
*/
void shuffle(int *array, size_t n)
{
    if (n > 1) 
    {
        size_t i;
        for (i = 0; i < n - 1; i++) 
        {
          size_t j = i + rand() / (RAND_MAX / (n - i) + 1);
          int t = array[j];
          array[j] = array[i];
          array[i] = t;
        }
    }
}

int active_flag_valid() {
    Task* head = task_pool.active_list_head;
    int flag = 1;
    while(head) {
        flag = flag && head->active;
        head = head->pool_next;
    }
    head = task_pool.free_list_head;
    while(head) {
        flag = flag && !head->active;
        head = head->pool_next;
    }
    return flag;
}

static int generic_count(Task* head) {
    int count = 1;

    if(!head) { return 0; }

    // TODO should check that all are either active or  not
    while(head->pool_next) {
        count += 1;
        head = head->pool_next;
        if(count > POOL_DEPTH) {
         // This SHOULD never happen
            break;
        }
    }
    return count;
}

static int count_free() {
    return generic_count(task_pool.free_list_head);
}

static int count_active() {
    return generic_count(task_pool.active_list_head);
}


static void test_alloc_one_dealloc_one() {
    printf("Starting Alloc one dealloc one test\n");
    initialize_pool();

    Task* task = task_pool_alloc();

    assert(task_pool.num_active  == count_active());
    assert(task_pool.num_active == 1);

    assert(task_pool.num_free  == count_free());
    assert(task_pool.num_free == POOL_DEPTH - 1);
    assert(active_flag_valid());

    task_pool_free(task);

    assert(task_pool.num_active  == count_active());
    assert(task_pool.num_active == 0);

    assert(task_pool.num_free  == count_free());
    assert(task_pool.num_free == POOL_DEPTH);
    assert(active_flag_valid());
    printf("Passes Alloc one dealloc one test\n\n");
}

static void test_initialize() {
    printf("Starting Initialization test\n");
    initialize_pool();
    //print_tasks();

    assert(task_pool.num_active  == count_active());
    assert(task_pool.num_active == 0);

    assert(task_pool.num_free  == count_free());
    assert(task_pool.num_free == POOL_DEPTH);
    assert(active_flag_valid());

    printf("Passes Initialization test\n\n");
}

static void test_alloc_many_dealloc_many(int how_many, int repeat) {

    int i, j;
    Task* tasks[POOL_DEPTH];

    initialize_pool();
    if(repeat <= 0) {
        repeat =1;
    }
    printf("Starting Alloc %i Dealloc %i test\n", how_many, how_many);
    for(j=0; j < repeat; j++) {
        for(i=0; i <how_many; i++) {
            tasks[i] = task_pool_alloc();

            assert(task_pool.num_active  == count_active());
            assert(task_pool.num_active == i+1);

            assert(task_pool.num_free  == count_free());
            assert(task_pool.num_free == POOL_DEPTH - (i+1));
            assert(active_flag_valid());
        }
        for(;i > 0; i--) {
            task_pool_free(tasks[how_many - i]);

            assert(task_pool.num_active  == count_active());
            assert(task_pool.num_active == i-1);

            assert(task_pool.num_free  == count_free());
            assert(task_pool.num_free == POOL_DEPTH - (i-1));
            assert(active_flag_valid());
        }
    }
    printf("Passes Alloc %i Dealloc %i test\n\n", how_many, how_many);
}

static void test_dealloc_empty() {
    printf("Starting Dealloc Zero test\n");
    initialize_pool();

    task_pool_free(NULL);
    assert(task_pool.num_active  == count_active());
    assert(task_pool.num_active == 0);

    assert(task_pool.num_free  == count_free());
    assert(task_pool.num_free == POOL_DEPTH);
    assert(active_flag_valid());

    printf("Passes Dealloc Zero test\n\n");
}

static void test_double_free() {
    printf("Starting Double free test\n");
    initialize_pool();

    Task* task = task_pool_alloc();
    Task* task2 = task_pool_alloc();

    task_pool_free(task);
    task_pool_free(task);

    assert(task_pool.num_active  == count_active());
    assert(task_pool.num_active == 1);

    assert(task_pool.num_free  == count_free());
    assert(task_pool.num_free == POOL_DEPTH -1);
    assert(active_flag_valid());

    printf("Passes Double  free test\n\n");
}

static void test_alloc_too_many() {
    int i;
    Task* tasks[POOL_DEPTH];
    initialize_pool();
    printf("Starting Alloc TOO many test\n");

    for(i=0; i<POOL_DEPTH; i++) {
        tasks[i] = task_pool_alloc();
    }
    Task* one_too_far = task_pool_alloc();
    assert(one_too_far == NULL);

    assert(task_pool.num_active  == count_active());
    assert(task_pool.num_active == POOL_DEPTH);

    assert(task_pool.num_free  == count_free());
    assert(task_pool.num_free == 0);
    assert(active_flag_valid());


    // Now dealloc one and make sure the count goes down
    task_pool_free(tasks[--i]);
    assert(task_pool.num_active  == count_active());
    assert(task_pool.num_active == POOL_DEPTH-1);

    assert(task_pool.num_free  == count_free());
    assert(task_pool.num_free == 1);
    assert(active_flag_valid());
    printf("Passes Alloc TOO many\n\n");
}

static void test_random_order_alloc_dealloc(int n) {
    int i;
    int* idx_array;
    Task* tasks[POOL_DEPTH];
    if(n > POOL_DEPTH) {
        printf("Can't run random order test with more than %i tasks, %i requested\n", POOL_DEPTH, n);
        return;
    }
    printf("Starting random order alloc dealloc\n");

    initialize_pool();
    idx_array = malloc(n*sizeof(int));
    for(i=0; i<n; i++) {
        idx_array[i] = i;
    }

    shuffle(idx_array, n);

    for(i=0; i < n;i++) {
        tasks[idx_array[i]] = task_pool_alloc();
    }
    // Now free half the tasks
    for(; i > n/2; i--) {
        task_pool_free(tasks[i-1]);
    }
    int expected = n/2; // I think this is true regardless of if n is odd or even

    assert(task_pool.num_active  == count_active());
    assert(task_pool.num_active == expected);

    assert(task_pool.num_free  == count_free());
    assert(task_pool.num_free == POOL_DEPTH - expected);
    assert(active_flag_valid());

    // Now allocate a few more
    Task* task_temp[3];
    for(int j =0; j < 3; j++) {
        task_temp[j] = task_pool_alloc();
        expected +=1;
        assert(task_pool.num_active  == count_active());
        assert(task_pool.num_active == expected);

        assert(task_pool.num_free  == count_free());
        assert(task_pool.num_free == POOL_DEPTH - expected);
        assert(active_flag_valid());
    }

    // Now dealloc the rest
    for(; i > 0; i--) {
        task_pool_free(tasks[i-1]);
        expected -=1;
        assert(task_pool.num_active  == count_active());
        assert(task_pool.num_active == expected);

        assert(task_pool.num_free  == count_free());
        assert(task_pool.num_free == POOL_DEPTH - expected);
        assert(active_flag_valid());
    }

    task_pool_free(task_temp[2]);
    task_pool_free(task_temp[1]);
    task_pool_free(task_temp[0]);
    assert(task_pool.num_active  == count_active());
    assert(task_pool.num_active == 0);

    assert(task_pool.num_free  == count_free());
    assert(task_pool.num_free == POOL_DEPTH);
    assert(active_flag_valid());

    printf("Passes random order alloc dealloc\n\n");
    free(idx_array);
}

int main() {
    test_initialize();
    test_alloc_one_dealloc_one();
    test_alloc_many_dealloc_many(420, 0);
    test_alloc_many_dealloc_many(1024, 0);
    test_alloc_many_dealloc_many(1024, 5);

    test_dealloc_empty();
    test_double_free();
    test_alloc_too_many();
    test_random_order_alloc_dealloc(900);


    printf("All tests passed %s %s\n", check, thumbs_up);

    return 0;
}
