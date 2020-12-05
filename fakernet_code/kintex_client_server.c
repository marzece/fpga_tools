#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "fnet_client.h"
#include "iic.h"
#include "gpio.h"
#include "ads_if.h"
#include "lmk_if.h"
#include "dac_if.h"

// For doing "double" reads the 2nd read should be from this register
#define SAFE_READ_ADDRESS 0x0

int dummy_mode = 0;

struct fnet_ctrl_client* fnet_client;
FILE* debug_file;
char* fpga_cli_hint_str = NULL;

const int BUFFER_SIZE = 1024;
char command_buffer[BUFFER_SIZE];
char resp_buffer[BUFFER_SIZE];
static volatile int end_main_loop = 0;
typedef uint32_t (*CLIFunc)(uint32_t* args);

typedef struct ServerCommand {
    const char* name;
    CLIFunc func;
    int nargs;
} ServerCommand;

int setup_udp() {
    debug_file = fopen("fakernet_debug_log.txt", "r");
    const char* fnet_hname = "192.168.1.192";
    int reliable = 0; // wtf does this do?
    const char* err_string = NULL;
    if(dummy_mode) {
        return 0;
    }

    fnet_client = fnet_ctrl_connect(fnet_hname, reliable, &err_string, debug_file);
    if(!fnet_client) {
        printf("ERROR Connecting!\n");
        return -1;
    }
    return 0;
}

void sig_handler(int dummy) {
    (void) dummy;
    if(!end_main_loop) {
        end_main_loop = 1;
    }
    else {
        exit(0);
    }
}


uint32_t read_addr(uint32_t base, uint32_t addr) {
      fakernet_reg_acc_item *send;
      fakernet_reg_acc_item *recv;

      if(dummy_mode) {
          return 0xDEADBEEF;
      }

      fnet_ctrl_get_send_recv_bufs(fnet_client, &send, &recv);

      addr = base + addr;
      addr &= 0x3FFFFFF; // Only the first 25-bits are valid

      send[0].data = htonl(0x0);
      send[0].addr = htonl(FAKERNET_REG_ACCESS_ADDR_READ | addr);

      int num_items = 1;
      int ret = fnet_ctrl_send_recv_regacc(fnet_client, num_items);
      
      // Pretty sure "ret" will be the number of UDP reg-accs dones
      if(ret == 0) {
          printf("ERROR %i\n", ret);
          printf("%s\n", fnet_ctrl_last_error(fnet_client));
          printf("%s\n", strerror(errno));
          return -1;
      }
      return ntohl(recv[0].data);
}

// My M_AXI Reg Mem IF has a 1-read latency.
// I.e. For each read request it returns the read result from the previous read.
// So by doing a "double" read I can get around this problem, but this kinda assumes reading
// is idempotent
// A better solution would be to have a super-secret address that says just return
// whatever the most recent read result was. Then I do a read from the desired address,
// then I read from that super-secret address
uint32_t double_read_addr(uint32_t base, uint32_t addr) {
    read_addr(base, addr);
    usleep(100);
    return read_addr(SAFE_READ_ADDRESS, 0x0);
}

int write_addr(uint32_t base, uint32_t addr, uint32_t data) {
      fakernet_reg_acc_item *send;
      fakernet_reg_acc_item *recv;

      if(dummy_mode) {
          return 0;
      }

      addr = addr+base;
      fnet_ctrl_get_send_recv_bufs(fnet_client, &send, &recv);

      addr &= 0x3FFFFFF; // Only the first 25-bits are valid

      send[0].data = htonl(data);
      send[0].addr = htonl(FAKERNET_REG_ACCESS_ADDR_WRITE | addr);

      int num_items = 1;
      int ret = fnet_ctrl_send_recv_regacc(fnet_client, num_items);
      
      // Pretty sure "ret" will be the number of UDP reg-accs dones
      if(ret == 0) {
          printf("ERROR %i\n", ret);
          printf("%s\n", fnet_ctrl_last_error(fnet_client));
          printf("%s\n", strerror(errno));
          return -1;
      }
      return 0;
}

uint32_t write_addr_command(uint32_t* args) {
    return write_addr(args[0], 0, args[1]);
}

uint32_t read_addr_command(uint32_t* args) {
    return double_read_addr(args[0], 0);
}

uint32_t sleep_command(uint32_t* args) {
    return sleep(args[0]);
}

static ServerCommand commandTable[] = {
    {"write_addr", write_addr_command, 2},
    {"read_addr", read_addr_command, 1},
    {"read_iic_reg", read_iic_block_command, 1},
    {"write_iic_reg", write_iic_block_command, 2},
    {"read_iic_bus", read_iic_bus_command, 1},
    {"write_iic_bus", write_iic_bus_command, 2},
    {"read_iic_bus_with_reg", read_iic_bus_with_reg_command, 2},
    {"write_iic_bus_with_reg", write_iic_bus_with_reg_command, 3},
    {"read_gpio0", read_gpio_0_command, 1},
    {"write_gpio0", write_gpio_0_command, 2},
    {"read_gpio1", read_gpio_1_command, 1},
    {"write_gpio1", write_gpio_1_command, 2},
    {"read_gpio2", read_gpio_2_command, 1},
    {"write_gpio2", write_gpio_2_command, 2},
    {"set_preamp_power", set_preamp_power_command, 1},
    {"set_adc_power", set_adc_power_command, 1},
    {"read_ads_a", read_ads_a_if_command, 1},
    {"write_ads_a", write_ads_a_if_command, 2},
    {"read_ads_b", read_ads_b_if_command, 1},
    {"write_ads_b", write_ads_b_if_command, 2},
    {"write_a_adc_spi", write_a_adc_spi_command, 3},
    {"write_b_adc_spi", write_b_adc_spi_command, 3},
    {"ads_a_spi_data_available", ads_a_spi_data_available_command, 0},
    {"ads_b_spi_data_available", ads_b_spi_data_available_command, 0},
    {"ads_a_spi_pop", ads_a_spi_data_pop_command, 0},
    {"ads_b_spi_pop", ads_b_spi_data_pop_command, 0},
    {"write_lmk_if", write_lmk_if_command, 2},
    {"read_lmk_if", read_lmk_if_command, 1},
    {"write_lmk_spi", write_lmk_spi_command, 3},
    {"lmk_spi_data_available", lmk_spi_data_available_command, 0},
    {"lmk_spi_data_pop", lmk_spi_data_pop_command, 0},
    {"read_dac_if", read_dac_if_command, 1},
    {"write_dac_if", write_dac_if_command, 2},
    {"write_dac_spi", write_dac_spi_command, 3},
    {"set_bias_for_channel", set_bias_for_channel_command, 2},
    {"set_ocm_for_channel", set_ocm_for_channel_command, 2},
    {"toggle_dac_ldac", toggle_dac_ldac_command, 0},
    {"toggle_dac_reset", toggle_dac_reset_command, 0},
    {"sleep", sleep_command, 1},
    {"", NULL, 0} // Must be last
};

int handle_line(const char* line) {
    // first check if the first char is a '#' or the line is empty
    // if it is, treat this as a comment
    if(strlen(line) == 0 || line[0] == '#') {
        return 0;
    }
    char* command_name = NULL;
    char* arg_buff[11]= {command_name, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    char* line_copy = malloc(sizeof(char)*(strlen(line)+1));
    strcpy(line_copy, line);
    command_name = strtok(line_copy, " \t\r\n"); // TODO include all WS characters
    arg_buff[0] = command_name;
    int nargs = 0;
    while(nargs-1 < 10) {
        arg_buff[nargs+1] = strtok(NULL, " \t\r\n");
        // TODO add empty string check
        if( arg_buff[nargs+1] == NULL) {
            break;
        }
        nargs +=1;
    }

    int cmd_index =0;
    ServerCommand* command = NULL;
    while(commandTable[cmd_index].func != NULL) {
        if(strcmp(commandTable[cmd_index].name, command_name) == 0) {
            command = &(commandTable[cmd_index]);
            break;
        }
        cmd_index +=1;
    }
    if(commandTable[cmd_index].func == NULL) {
        snprintf(resp_buffer, BUFFER_SIZE,"Invalid/Unknown command given\n");
        return 0;
    }

    if(command->nargs != nargs) {
        // Too few arguments
        // TODO send back error message
        snprintf(resp_buffer, BUFFER_SIZE, "Err: Command \"%s\" requires %i arguments, %i given.\n",command_name, command->nargs, nargs);
        return 0;
    }

    int i;
    uint32_t *args = malloc(sizeof(uint32_t)*nargs);
    for(i=0; i< nargs; i++) {
        args[i] = strtoul(arg_buff[i+1], NULL, 0);
    }

    uint32_t ret = command->func(args);
    snprintf(resp_buffer, BUFFER_SIZE, "0x%x\n", ret);
    free(args);
    return 0;
}

int main(int argc, char** argv) {

    if(argc > 1) {
        if(strcmp(argv[1], "--dry") == 0 || strcmp(argv[1], "--dummy") == 0) {
            printf("DUMMY MODE ENGAGED\n");
            dummy_mode = 1;
        }
    }

    // First connect to FPGA
    if(setup_udp()) {
        printf("error ocurred connecting to fpga\n");
        //return 1;
    }

    memset(resp_buffer, 0, BUFFER_SIZE);
    memset(command_buffer, 0, BUFFER_SIZE);
    signal(SIGINT , sig_handler);

    // Open up pipes to receive and respond to commands
    const char* command_fn = "kintex_command_pipe";
    const char* resp_fn = "kintex_response_pipe";

    if(mkfifo(command_fn, 0666)) {
        printf("Error making command pipe\n");
        return 1;
    }
    if(mkfifo(resp_fn, 0666)) {
        printf("Error making response pipe\n");
        return 1;
    }
    int _recv_fd = open(command_fn, O_RDONLY);
    if(_recv_fd <= 0) {
        printf("Error ocurred opening command recieve pipe\n");
        printf("%s\n", strerror(errno));
        return 1;
    }
    // This will block until someone connects to the pipe!
    // TODO should maybe just sleep here and wait for the resp_channel to be
    // avialble
    int _resp_fd = open(resp_fn, O_WRONLY );
    if(_resp_fd <= 0) {
        printf("Error ocurred opening response pipe\n");
        printf("%s\n", strerror(errno));
        close(_recv_fd);
        unlink(command_fn);
        unlink(resp_fn);
        return 1;
    }

    
    printf("Starting main loop\n");
    while(!end_main_loop) {
        // TODO this assumes data is sent with a \0 terminator....
        // I should fix that.
        int nbytes = read(_recv_fd, command_buffer, BUFFER_SIZE);
        if(nbytes == 0) {
            continue;
        }
        if(nbytes >= BUFFER_SIZE-1) {
            printf("Large command recieved...I can't handle this\n");
            end_main_loop = 1;
            break;
        }


        // Make sure the command ends in a null terminator
        command_buffer[nbytes] = '\0';

        if(command_buffer[nbytes-1] != '\n'){
            printf("%s\n", command_buffer);

        } else {
            printf("%s", command_buffer);
        }

        // handle_line should always write it's results to the resp_buffer,
        // don't need to check if it returns 0 or not.
        handle_line(command_buffer);

        // resp_buffer should have a null terminator...don't need to send it though?
        write(_resp_fd, &resp_buffer, strlen(resp_buffer));
        usleep(1000);
    }

    printf("Cntrl-C found, quitting\n");
    close(_recv_fd);
    close(_resp_fd);
    unlink(command_fn);
    unlink(resp_fn);
    return 0;
}
