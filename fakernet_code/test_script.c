#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "fnet_client.h"

struct fnet_ctrl_client* fnet_client;
FILE* debug_file;

int setup_udp() {
    debug_file = fopen("fakernet_debug_log.txt", "r");
    const char* fnet_hname = "192.168.1.192";
    int reliable = 0; // wtf does this do?
    const char* err_string = NULL;
    fnet_client = fnet_ctrl_connect(fnet_hname, reliable, &err_string, debug_file);
    if(!fnet_client) {
        printf("ERROR Connecting!\n");
        return -1;
    }
    return 0;
}


int send_cmd(int read_nwrite, uint32_t addr, uint32_t data) {
      fakernet_reg_acc_item *send;
      fakernet_reg_acc_item *recv;

      fnet_ctrl_get_send_recv_bufs(fnet_client, &send, &recv);

      addr &= 0x3FFFFFF; // Only the first 25-bits are valid

      send[0].data = htonl(data);

      if(read_nwrite) {
          send[0].addr = htonl(FAKERNET_REG_ACCESS_ADDR_READ | addr);
      }
      else {
          send[0].addr = htonl(FAKERNET_REG_ACCESS_ADDR_WRITE | addr);
      }
      int num_items = 1;
      int ret = fnet_ctrl_send_recv_regacc(fnet_client, num_items);

      // Pretty sure "ret" will be the number of UDP reg-accs dones
      if(ret == 0) {
          printf("ERROR %i\n", ret);
          printf("%s\n", fnet_ctrl_last_error(fnet_client));
          printf("%s\n", strerror(errno));
          return -1;
      }
      printf("RECV[0x%x] = 0x%x\n", ntohl(recv[0].addr), ntohl(recv[0].data));

      return 0;
}

int main(int argc, char** argv) {
    // Args are read/not-write, addr, data
    if(argc != 4) {
        printf("%s: read/not-write addr data\n", argv[0]);
        return 0;
    }

    uint32_t read_nwrite = strtoul(argv[1], NULL, 0);
    uint32_t addr = strtoul(argv[2], NULL, 0);
    uint32_t data = strtoul(argv[3], NULL, 0);

    if(setup_udp()) {
        return -1;
    }

    send_cmd(read_nwrite, addr, data);

    fclose(debug_file);
    return 0;

}
