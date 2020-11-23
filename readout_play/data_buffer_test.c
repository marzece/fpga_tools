#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "data_buffer_v2.h"

#define DATA_BUF_EFF_LEN DATA_BUF_LEN

static const char thumbs_up[5] = {0xF0, 0x9F, 0x91, 0x8d, '\0'};
static const char check[4] = {0xE2, 0x9C, 0x85, '\0'};

static void test_initialize_buffer() {
    printf("Starting intialize buffer test\n");
    initialize_data_buffer();
    assert(data_buffer_contiguous_space_available() == data_buffer_space_available());
    assert(data_buffer_contiguous_space_used() == data_buffer_space_used());
    assert(data_buffer_space_available() == DATA_BUF_EFF_LEN);
    assert(data_buffer_space_used() == 0);

    printf("Passed intialize buffer test\n\n");
}

static void test_push_one_item() {
    printf("Starting one item test\n");
    initialize_data_buffer();

    push_to_buffer(0xFFFFFFFF);

    assert(data_buffer_contiguous_space_available() == data_buffer_space_available());
    assert(data_buffer_contiguous_space_used() == data_buffer_space_used());
    assert(data_buffer_space_available() == DATA_BUF_EFF_LEN -4);


    assert(data_buffer_space_used() == 4);

    printf("Passed one item  test\n\n");
}
static void test_register_one_item() {
    printf("Starting one item test\n");
    initialize_data_buffer();

    data_buffer_register_writes(4);

    assert(data_buffer_contiguous_space_available() == data_buffer_space_available());
    assert(data_buffer_contiguous_space_used() == data_buffer_space_used());
    assert(data_buffer_space_available() == DATA_BUF_EFF_LEN -4);
    assert(data_buffer_space_used() == 4);

    printf("Passed one item  test\n\n");
}

static void test_register_many_items(const int n) {
    printf("Starting register %i items\n", n);

    assert(n <= DATA_BUF_EFF_LEN);
    initialize_data_buffer();

    data_buffer_register_writes(n);

    assert(data_buffer_contiguous_space_available() == data_buffer_space_available());
    assert(data_buffer_contiguous_space_used() == data_buffer_space_used());
    assert(data_buffer_space_available() == DATA_BUF_EFF_LEN - n);
    assert(data_buffer_space_used() == n);

    data_buffer_register_reads(n);

    assert(data_buffer_contiguous_space_available() != DATA_BUF_EFF_LEN - n);
    assert(data_buffer_contiguous_space_used() == 0);
    assert(data_buffer_space_used() == 0);
    assert(data_buffer_space_available() == DATA_BUF_EFF_LEN);

    printf("Passed register %i items\n\n", n);

}
static void test_push_many_items(const int n) {
    int i;
    printf("Starting push %i items\n", n);
    assert(4*n <= DATA_BUF_EFF_LEN);
    initialize_data_buffer();

    for(i=0; i<n; i++) {
        push_to_buffer(0xDEADBEEF);
    }

    assert(data_buffer_contiguous_space_available() == data_buffer_space_available());
    assert(data_buffer_contiguous_space_used() == data_buffer_space_used());
    assert(data_buffer_space_available() == DATA_BUF_EFF_LEN - 4*n);
    assert(data_buffer_space_used() == 4*n);
    printf("Passed push %i items\n\n", n);
}

static void test_stochastic_read(const int n) {
    // The idea here is to right a bunch, then do a bunch of reads
    int inc, r;
    int n_left = n;

    assert(n <= DATA_BUF_EFF_LEN);

    printf("Starting stochastic read %i items\n", n);
    initialize_data_buffer();
    
    if(n>16) {
        push_to_buffer(0xDEADBEEF);
        push_to_buffer(0xDEADBEEF);
        push_to_buffer(0xDEADBEEF);
        push_to_buffer(0xDEADBEEF);
        data_buffer_register_writes(n-16);
    }
    else {
        data_buffer_register_writes(n);
    }

    while(data_buffer_contiguous_space_used() > 0) {
        // I want read a random amount that's at most 10% of the remaining buffer
        // and at least 1 byte
        if(n_left > 10) {
            inc = n_left/10;
            r = rand() % inc;
            r = r > 0 ? r : 1;
        } else {
            r = 1;
        }
        data_buffer_register_reads(r);
        n_left -= r;
        assert(n_left >=0);

        if(n_left > 0) {
            assert(data_buffer_contiguous_space_available() == DATA_BUF_EFF_LEN - n);
            assert(data_buffer_contiguous_space_used() == n_left);
            assert(data_buffer_space_available() == DATA_BUF_EFF_LEN -n_left);
            assert(data_buffer_space_used() == n_left);
        }
    }
    printf("Passed stochastic read %i items\n\n", n);
}

static void test_exactly_full() {
    printf("Starting exactly full test\n");
    initialize_data_buffer();

    data_buffer_register_writes(DATA_BUF_LEN);
    
    assert(data_buffer_contiguous_space_used() == DATA_BUF_LEN);
    assert(data_buffer_space_used() == DATA_BUF_LEN);

    printf("Passed exactly full test\n\n");
}


static void test_clean_write_over_wrap() {
    printf("Starting clean write over wrap much\n");

    initialize_data_buffer();

    data_buffer_register_writes(DATA_BUF_EFF_LEN);

    assert(data_buffer_contiguous_space_available() == 0);
    assert(data_buffer_contiguous_space_used() == DATA_BUF_EFF_LEN);
    assert(data_buffer_space_available() == 0);
    assert(data_buffer_space_used() == DATA_BUF_EFF_LEN);

    int n_read = 40;
    int n_write = 20;
    data_buffer_register_reads(n_read);

    assert(data_buffer_contiguous_space_available() == n_read);
    assert(data_buffer_contiguous_space_used() == DATA_BUF_EFF_LEN - n_read);
    assert(data_buffer_space_available() == n_read);
    assert(data_buffer_space_used() == DATA_BUF_EFF_LEN - n_read);

    data_buffer_register_writes(n_write);
    assert(data_buffer_contiguous_space_available() == n_read - n_write);
    assert(data_buffer_contiguous_space_used() == DATA_BUF_EFF_LEN - n_read);
    assert(data_buffer_space_available() == n_read - n_write);
    assert(data_buffer_space_used() == DATA_BUF_EFF_LEN - n_read + n_write);

    data_buffer_register_reads(DATA_BUF_EFF_LEN - n_read);

    assert(data_buffer_space_available() == DATA_BUF_EFF_LEN - n_write);
    assert(data_buffer_space_used() ==   n_write);
    assert(data_buffer_contiguous_space_available() == DATA_BUF_EFF_LEN - n_write);
    assert(data_buffer_contiguous_space_used() == n_write);

    data_buffer_register_reads(n_write);
    assert(data_buffer_contiguous_space_available() == data_buffer_space_available());
    assert(data_buffer_contiguous_space_used() == data_buffer_space_used());
    assert(data_buffer_space_available() == DATA_BUF_EFF_LEN);
    assert(data_buffer_space_used() == 0);
    printf("Passed clean write over wrap\n\n");
}

static void test_dirty_write_over_wrap() {
    printf("Starting dirty write over wrap much\n");

    initialize_data_buffer();

    data_buffer_register_writes(DATA_BUF_EFF_LEN - 3);

    assert(data_buffer_contiguous_space_available() == 3);
    assert(data_buffer_contiguous_space_used() == DATA_BUF_EFF_LEN -3);
    assert(data_buffer_space_available() == 3);
    assert(data_buffer_space_used() == DATA_BUF_EFF_LEN -3);

    int n_read = 40;
    data_buffer_register_reads(n_read);

    assert(data_buffer_contiguous_space_available() == 3);
    assert(data_buffer_contiguous_space_used() == DATA_BUF_EFF_LEN - n_read - 3);
    assert(data_buffer_space_available() == n_read + 3);
    assert(data_buffer_space_used() == DATA_BUF_EFF_LEN -3 - n_read);

    assert(!push_to_buffer(0xDEADBEEF));

    assert(data_buffer_contiguous_space_available() == n_read - 1);
    assert(data_buffer_contiguous_space_used() == DATA_BUF_EFF_LEN - n_read);
    assert(data_buffer_space_available() == n_read  - 1);
    assert(data_buffer_space_used() == DATA_BUF_EFF_LEN - n_read + 1);

    printf("Passed dirty write over wrap\n\n");
}

static void test_many_random_writes() {

    printf("Starting many random writes\n");

    initialize_data_buffer();

    const int N_OPS = 1e8;
    const int MAX_SIZE = DATA_BUF_EFF_LEN/2;
    int expected_buff_size = 0;
    int i;
    int write_op;
    for(i=0; i<N_OPS; i++) {
        write_op = rand() % 2;
        assert(data_buffer_space_available()  == DATA_BUF_EFF_LEN - expected_buff_size);
        assert( data_buffer_space_used()  ==  expected_buff_size);
        int n = rand() % MAX_SIZE;
        if(write_op) {
            if(expected_buff_size + n > DATA_BUF_EFF_LEN) {
                n = DATA_BUF_EFF_LEN - expected_buff_size;
            }
            data_buffer_register_writes(n);
            expected_buff_size += n;
        }
        else {
            if(expected_buff_size - n < 0) {
                n = expected_buff_size;
            }
            data_buffer_register_reads(n);
            expected_buff_size -= n;
        }
    }


    printf("Passed many random writes\n\n");
}

int main() {
    srand(time(NULL));

    test_initialize_buffer();
    test_push_one_item();
    test_register_one_item();

    test_push_many_items(4200);
    test_register_many_items(18123);
    test_stochastic_read(18124);

    test_exactly_full();

    test_clean_write_over_wrap();
    test_dirty_write_over_wrap();
    test_many_random_writes();
    printf("All tests passed %s %s\n", check, thumbs_up);
    return 0;
}
