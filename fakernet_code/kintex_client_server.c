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
#include "server_common.h"
#include "hermes_if.h"
#include "ti_board_if.h"
#include "resp.h"
#include "server.h"

// For doing "double" reads the 2nd read should be from this register
#define COMMAND_PIPE_NAME "kintex_command_pipe"
#define RESPONSE_PIPE_NAME "kintex_response_pipe"
#define BUFFER_SIZE 2048
#define SEND_COMMAND_TABLE_COMMAND "send_command_table"

int dummy_mode = 0;
uint32_t SAFE_READ_ADDRESS;

struct fnet_ctrl_client* fnet_client;
FILE* debug_file;
char* fpga_cli_hint_str = NULL;

char command_buffer[BUFFER_SIZE];
char resp_buffer[BUFFER_SIZE];
static volatile int end_main_loop = 0;
ServerCommand* board_specific_command_table = NULL;

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
        return;
    }
    // Want to make sure these pipes get cleaned up no matter what
    unlink(RESPONSE_PIPE_NAME);
    unlink(COMMAND_PIPE_NAME);
    exit(0);
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
    {"write_addr", write_addr_command, 2, 1},
    {"read_addr", read_addr_command, 1, 1},
    {"sleep", sleep_command, 1, 1},
    {"", NULL, 0, 0} // Must be last
};

ServerCommand* search_for_command(ServerCommand* table, const char* command_name) {
    int cmd_index = 0;
    while(table[cmd_index].func != NULL) {
        if(strcmp(table[cmd_index].name, command_name) == 0) {
            break;
        }
        cmd_index +=1;
    }
    return &(table[cmd_index]);
}

// This function is a little hacked together, if it returns NULL then
// the calling function assumes the response was stuffed in to the "resp_buffer"
// data buffer.
// If it returns non-NULL then the calling function assumes the given char*
// is the response (and it's NULL terminated)
char* handle_line(const char* line) {
    // first check if the first char is a '#' or the line is empty
    // if it is, treat this as a comment
    if(strlen(line) == 0 || line[0] == '#') {
        return 0;
    }
    int i;
    char* command_name = NULL;
    char* arg_buff[11]= {command_name, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    char* line_copy = malloc(sizeof(char)*(strlen(line)+1));
    strcpy(line_copy, line);
    command_name = strtok(line_copy, " \t\r\n");
    arg_buff[0] = command_name;
    int nargs = 0;
    while(nargs-1 < 10) {
        arg_buff[nargs+1] = strtok(NULL, " \t\r\n");
        // TODO add empty string check
        if(arg_buff[nargs+1] == NULL) {
            break;
        }
        nargs +=1;
    }

    ServerCommand* command = NULL;
    if(strcmp(command_name, SEND_COMMAND_TABLE_COMMAND) == 0) {
        // send command table is special!
        char* return_string = NULL;
        char** commands_buf = NULL;
        // TODO BUF_SIZE is basically already used....should I just use the standard resp_buffer?
        const int BUF_SIZE = 1024;
        char buf[BUF_SIZE];
        int bytes_count = 0;
        int bytes;
        char* write_location;
        int command_count = 0;

        for(i=0; i<2; i++) {
            command = i==0 ? commandTable : board_specific_command_table;
            while(command->func != NULL) {
                command_count += 1;
                commands_buf = realloc(commands_buf, sizeof(char*)*command_count);
                // TODO I guess I should check the return value of snprintf...but whatevs
                snprintf(buf, BUF_SIZE, "+%s %i\r\n", command->name, command->nargs);
                bytes = strlen(buf);
                commands_buf[command_count-1] = malloc(sizeof(char)*(bytes + 1));
                memcpy(commands_buf[command_count-1], buf, bytes+1);

                bytes_count += bytes;
                command += 1;
            }
        }
        snprintf(buf, BUF_SIZE, "*%i\r\n", command_count);
        return_string = malloc(strlen(buf) + bytes_count + 1); // The +1 is for the null terminator
        write_location = return_string;
        memcpy(write_location, buf, strlen(buf));
        write_location += strlen(buf);

        for(i=0; i<command_count; i++) {
            bytes = strlen(commands_buf[i]);
            memcpy(write_location, commands_buf[i], bytes);
            write_location += bytes;
            free(commands_buf[i]);
        }
        free(commands_buf);
        return return_string;
    }

    // First search the board specific commands
    command = search_for_command(board_specific_command_table, command_name);
    if(command->func == NULL) {
        // Then search the "standard" commands
        command = search_for_command(commandTable, command_name);
    }
    if(command->func == NULL) {
        snprintf(resp_buffer, BUFFER_SIZE,"-Invalid/Unknown command given\r\n");
        return 0;
    }

    if(command->nargs != nargs) {
        // Too few arguments
        // TODO send back error message
        snprintf(resp_buffer, BUFFER_SIZE, "-Err: Command \"%s\" requires %i arguments, %i given.\r\n",command_name, command->nargs, nargs);
        return NULL;
    }

    int array_size = nargs > command->nresp ? nargs : command->nresp;
    uint32_t *args = malloc(sizeof(uint32_t)*array_size);
    for(i=0; i< nargs; i++) {
        args[i] = strtoul(arg_buff[i+1], NULL, 0);
    }

    uint32_t ret = command->func(args);
    int bytes_written = 0;
    if(command->nresp <= 0) {
        bytes_written = snprintf(resp_buffer, BUFFER_SIZE, "+OK\r\n");
    }
    else if(command->nresp == 1) {
        bytes_written = resp_uint32(resp_buffer, BUFFER_SIZE, ret);
        //snprintf(resp_buffer, BUFFER_SIZE, "0x%x\n", ret);
    } else {
        bytes_written = resp_array(resp_buffer, BUFFER_SIZE, args, command->nresp);
    }

    if(bytes_written >= BUFFER_SIZE) {
        snprintf(resp_buffer, BUFFER_SIZE, "-Resp Buffer overflow!\r\n");
    }

    free(args);
    return NULL;
}

int main(int argc, char** argv) {

    int ti_board=0;
    if(argc > 1) {
        if(strcmp(argv[1], "--dry") == 0 || strcmp(argv[1], "--dummy") == 0) {
            printf("DUMMY MODE ENGAGED\n");
            dummy_mode = 1;
        }
        else if(strcmp(argv[1], "--ti") == 0 || strcmp(argv[1], "--TI") ==0) {
            printf("Using TI board commands\n");
            ti_board = 1;
        }
    }

    // First connect to FPGA
    if(setup_udp()) {
        printf("error ocurred connecting to fpga\n");
        //return 1;
    }

    memset(resp_buffer, 0, BUFFER_SIZE);
    memset(command_buffer, 0, BUFFER_SIZE);
    signal(SIGINT, sig_handler);


    // Set up command_tables
    board_specific_command_table = hermes_commands;
    SAFE_READ_ADDRESS = HERMES_SAFE_READ_ADDRESS;
    if(ti_board) {
        SAFE_READ_ADDRESS = TI_SAFE_READ_ADDRESS;
        board_specific_command_table = ti_commands;
    }

    //init_server(server);


    
    printf("Starting main loop\n");
    while(!end_main_loop) {
        //int nbytes = read(_recv_fd, command_buffer, BUFFER_SIZE);
        int nbytes = 0;
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
        char* resp = handle_line(command_buffer);


        usleep(1000);
    }

    printf("Cntrl-C found, quitting\n");
    return 0;
}
