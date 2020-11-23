#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <err.h>
#include <sys/errno.h>
#include <string.h>
#include <stdint.h>

static const char* fake_fifo_input_fn = "my_fake_fpga_data";
static const char* fake_fifo_resp_fn = "fake_fpga_resp_channel";

#define  DBUF_SIZE 65536
char dbuf[DBUF_SIZE];
int main(void) {
    int recv_fd = 0;
    int resp_fd = 0;
    int nread;
    uint32_t resp;

    while(recv_fd <= 0) {
        recv_fd = open(fake_fifo_input_fn, O_RDONLY);
    }
    while(resp_fd <= 0) {
        resp_fd = open(fake_fifo_resp_fn, O_WRONLY);
    }

    printf("Recveiver running!\n");

    while(1) {
        nread = read(recv_fd, dbuf, DBUF_SIZE);
        if(nread < 0) {
            printf("read returned %i\n", nread);
        }
        if(nread == 0) {
            continue;
        }
        resp = nread;
        write(resp_fd, &resp, sizeof(uint32_t));
    }

}
