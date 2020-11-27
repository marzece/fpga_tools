#include "fake_lwip.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include<string.h>
#include<sys/errno.h>

#define BS_BUFFER_SIZE 65536
static int write_fd;
static int read_fd;
static const char* fake_fifo_ostream_fn = "my_fake_fpga_data";
static const char* fake_fifo_istream_fn = "fake_fpga_resp_channel";
static const int NUM_OBUFS = 16;

struct pbuf {
    void* data_ptr;
    u16_t len;
};
static struct pbuf obufs[NUM_OBUFS];
static int obufs_in_use = 0;
static int obuf_r_pointer = 0;
static int obuf_w_pointer = 0;

int tcp_write(struct tcp_pcb* client, void* data, u16_t len, int opts) {
    if(obufs_in_use == NUM_OBUFS) {
        return -11;
    }
    obufs[obuf_w_pointer].data_ptr = data;
    obufs[obuf_w_pointer].len = len;
    obuf_w_pointer = (obuf_w_pointer + 1) % NUM_OBUFS;
    obufs_in_use +=1;
    return 0;
}

int tcp_output(struct tcp_pcb* client) {
    // This function dumps all the data....which isn't totally realistic
    // but doing anything else would be complicated and hard sooooooo....
    // fuckit dude
    ssize_t bytes_written;
    struct pbuf* this_obuf;
    while(obufs_in_use > 0) {
        this_obuf = &(obufs[obuf_r_pointer]);
        bytes_written = write(write_fd, this_obuf->data_ptr, this_obuf->len);
        if(bytes_written < 0) {
            printf("writing error: %s\n", strerror(errno));
            continue;
        }
        if(bytes_written == this_obuf->len) {
            // Reset the obuf
            this_obuf->data_ptr = NULL;
            this_obuf->len = 0;
            obuf_r_pointer = (obuf_r_pointer + 1) % 16;
            obufs_in_use -= 1;
        } else {
            // Move the data_ptr forward and subtract the amount left
            this_obuf->data_ptr += bytes_written;
            this_obuf->len -= bytes_written;
        }
    }
    return 0;
}

uint32_t check_netif() {
    // Receieve ack's from 'client'.
    // Ack should just be a 32-bit integer saying how much data
    // was recvd
    ssize_t nread;
    uint32_t ret;
    nread = read(read_fd, &ret, sizeof(uint32_t));
    if(nread > 0) {
        return ret;
    }
    if(nread == 0) {
        printf("EOF found\n");
        exit(1);
    }
    return 0;
}

int tcp_sndbuf(struct tcp_pcb* client) {
    return 8192;
}

void initialize_lwip() {

    if(mkfifo(fake_fifo_ostream_fn, 0666)) {
        printf("could not make fifo\n");
        printf("%s\n", strerror(errno));

    }
    if(mkfifo(fake_fifo_istream_fn, 0666)) {
        printf("could not make fifo\n");
        printf("%s\n", strerror(errno));

    }
    write_fd = open(fake_fifo_ostream_fn, O_WRONLY | O_NONBLOCK);
    if(write_fd < 0) {
        printf("Error opening output file: %s\n", strerror(errno));
    }

    read_fd = open(fake_fifo_istream_fn, O_RDONLY | O_NONBLOCK);

    for(int i=0; i<NUM_OBUFS; i++) {
        obufs[i].data_ptr = NULL;
        obufs[i].len = 0;
    }
    obufs_in_use = 0;
}

void clean_up_fds() {
    unlink(fake_fifo_istream_fn);
    unlink(fake_fifo_ostream_fn);
}
