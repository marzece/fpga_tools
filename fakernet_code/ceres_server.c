#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "fnet_client.h"
#include "server_common.h"
#include "server.h"
#include "util.h"
#include "ceres_if.h"
#include "resp.h"
#include "daq_logger.h"

// For doing "double" reads the 2nd read should be from this register
#define NUM_XEMS 8
#define BUFFER_SIZE 2048

#define LOGGER_NAME "ceres_server"
#define DEFAULT_REDIS_HOST "127.0.0.1"
#define LOG_FILENAME "ceres_server.log"
#define LOG_MESSAGE_MAX 1024

int verbosity_stdout = LOG_INFO;
int verbosity_file = LOG_INFO;
int verbosity_redis = LOG_WARN;


int dummy_mode = 0;
uint32_t SAFE_READ_ADDRESS;

typedef struct XEMConn {
    fnet_ctrl_client* fnet_client;
    int device_id;
    char* ip;
//    int is_up;
} XEMConn;
XEMConn XEMS[NUM_XEMS];

XEMS[0] = {NULL, 4, 192.168.1.196};
XEMS[1] = {NULL, 5, 192.168.1.197};
XEMS[2] = {NULL, 6, 192.168.1.198};
XEMS[3] = {NULL, 7, 192.168.1.199};
XEMS[4] = {NULL, 8, 192.168.1.200};
XEMS[5] = {NULL, 9, 192.168.1.201};
XEMS[6] = {NULL, 10, 192.168.1.202};
XEMS[7] = {NULL, 11, 192.168.1.203};

XEMConn* the_active_xem;

char command_buffer[BUFFER_SIZE];
char resp_buffer[BUFFER_SIZE];
static volatile int end_main_loop = 0;
ServerCommand* board_specific_command_table = NULL;


// This serverLog function should ONLY be called by code that I stole from
// redis. No one else should use it.
void serverLog(int level, const char *fmt, ...) {
    // This is a bit of a mess, but the daq_logger uses levels that I (Eric  Marzec)
    // came up with, and the server code uses log levels that
    // Antirez/Redis came up with.  But I want to get log messages from the
    // server code and shove those into my logger code.  So I need to do a bit
    // of fiddling to get the log message prioty values to line up in a
    // sensible way.
    switch(level){
        case(LL_DEBUG):
            level=LOG_DEBUG;
            break;
        case(LL_VERBOSE):
            level=LOG_INFO;
            break;
        case(LL_NOTICE):
            level=LOG_WARN;
            break;
        case(LL_WARNING):
            level=LOG_ERROR;
            break;
        default:
            level = LOG_ERROR;
    }
    va_list arglist;
    va_start(arglist, fmt);
        daq_log_raw(level, fmt, arglist);
    va_end(arglist);
}

void sig_handler(int dummy) {
    (void) dummy;
    if(!end_main_loop) {
        end_main_loop = 1;
        server.shutdown_asap=1;
        return;
    }
    exit(0);
}


int set_active_xem(uint32_t xem_bits) {

}

int setup_udp(const char* ip, XEMConn* xem) {
    int reliable = 0; // wtf does this do?
    const char* err_string = NULL;
    if(dummy_mode) {
        return 0;
    }

    xem->fnet_client = fnet_ctrl_connect(xem->ip, reliable, &err_string, NULL);
    if(!xem->fnet_client) {
        return -1;
    }
    return 0;
}

int read_addr(uint32_t base, uint32_t addr, uint32_t* result) {
    fakernet_reg_acc_item *send;
    fakernet_reg_acc_item *recv;

    if(dummy_mode) {
        *result = 0xDEADBEEF;
        return 0;
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
    *result = ntohl(recv[0].data);
    return 0;
}

// My M_AXI Reg Mem IF has a 1-read latency.
// I.e. For each read request it returns the read result from the previous read.
// So by doing a "double" read I can get around this problem, but this kinda assumes reading
// is idempotent
// A better solution would be to have a super-secret address that says just return
// whatever the most recent read result was. Then I do a read from the desired address,
// then I read from that super-secret address
int double_read_addr(uint32_t base, uint32_t addr, uint32_t* result) {

    read_addr(base, addr, result);
    usleep(100);
    return read_addr(SAFE_READ_ADDRESS, 0x0, result);
}

int write_addr(uint32_t base, uint32_t addr, uint32_t data) {
      fakernet_reg_acc_item *send;
      fakernet_reg_acc_item *recv;

      if(dummy_mode) { return 0; }

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

void write_addr_command(client* c, int argc, sds* args) {
    UNUSED(argc);
    //write_addr((char*)args[0], 0, args[1]);
    long addr;
    long val;
    char* valid;
    addr = strtol(c->argv[1], &valid, 0);
    // If strtol fails to convert the return is zero and valid is set equal to the given string
    // (errno should also be set but maybe not on all OSes. Should test (TODO)
    // Should also test for overflow/underflow too
    if(addr == 0 && valid == c->argv[1]) {
        addReplyErrorFormat(c, "'%s' is not a valid number", args[1]);
        return;
    }
    val = strtol(c->argv[2], &valid, 0);
    if(val == 0 && valid == c->argv[2]) {
        addReplyErrorFormat(c, "'%s' is not a valid number", args[2]);
        return;
    }

    write_addr(addr, 0, val);
    addReplyStatus(c, "OK");
}

void read_addr_command(client* c, int argc, sds* args) {
    UNUSED(argc);
    long value;
    uint32_t ret;
    char* valid;

    value = strtol(c->argv[1], &valid, 0);
    // If strtol fails to convert the return is zero and valid is set equal to the given string
    // (errno should also be set but maybe not on all OSes. Should test (TODO)
    // Should also test for overflow/underflow too
    if(value == 0 && valid == c->argv[1]) {
        addReplyErrorFormat(c, "'%s' is not a valid number", args[1]);
        return;
    }
    if(double_read_addr(value, 0, &ret)) {
        addReplyError(c, "failed to read value from FPGA.");
        return;
    }
    addReplyLongLong(c, ret);
}

void sleep_command(client* c, int argc, sds* args) {
    UNUSED(argc);
    // TODO remove this command, its no longer worth keeping and it doesn't
    // play well with the server.

    long long val;
    int valid = string2ll((char*) args[1], sdslen(args[1]), &val);
    if(!valid) {
        addReplyErrorFormat(c, "'%s' is not a valid number", args[1]);
        return;
    }
    sleep((unsigned int) val);
    addReplyStatus(c, "OK");
}

static ServerCommand default_commands[] = {
    {"write_addr", write_addr_command, NULL, 3, 1, 0, 0},
    {"read_addr", read_addr_command, NULL, 2, 1, 0, 0},
    {"sleep",  sleep_command, NULL, 2, 1, 0, 0},
    {"", NULL, NULL, 0, 0, 0, 0} // Must be last
};

ServerCommand* combine_command_tables() {
    /* First calculate the lenght of the "built in" commands, then of the board specific commands.
     Then create a alloc memory sufficient for both.
     Then copy. */
    ServerCommand* combined_table;
    ServerCommand* cmd;
    int num_built_in = sizeof(default_commands) / sizeof(ServerCommand);
    int num_board_specific = 0;

    cmd = board_specific_command_table;
    while(cmd->func || cmd->legacy_func) {
        cmd++;
    }
     // Add one to make sure we include the "NULL" terminator
    num_board_specific = cmd - board_specific_command_table + 1;
    num_built_in -= 1; // Subtract one b/c we don't wanna include the "NULL" terminator

    combined_table = malloc(sizeof(ServerCommand)*(num_built_in + num_board_specific));
    if(!combined_table) {
        return NULL;
    }
    memcpy(combined_table, default_commands, sizeof(ServerCommand)*num_built_in);
    memcpy(combined_table+num_built_in, board_specific_command_table, sizeof(ServerCommand)*num_board_specific);
    return combined_table;
}

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


// For arguements with a values
enum ArgIDs {
    ARG_NONE=0,
    ARG_IP,
    ARG_PORT
};

void print_help_message() {
    printf("You need help\n");
}

int main(int argc, char** argv) {

    const char* ip = DEFAULT_IP;
    int port = -1;
    int i;
    if(argc > 1 ) {
        enum ArgIDs expecting_value = 0;
        for(i=1; i < argc; i++) {
            if(!expecting_value) {
                if(strcmp(argv[i], "--ip") == 0) {
                    expecting_value = ARG_IP;
                }
                else if(strcmp(argv[i], "--port") == 0) {
                    expecting_value = ARG_PORT;
                }
                else if(strcmp(argv[i], "--dry") == 0 || strcmp(argv[i], "--dummy") == 0) {
                    printf("DUMMY MODE ENGAGED\n");
                    dummy_mode = 1;
                }
                else if((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-h") == 0)) {
                    print_help_message();
                    return 0;
                }
                else {
                    printf("Unrecognized option \"%s\"\n", argv[i]);
                    return 0;
                }
            } else {
                switch(expecting_value) {
                    case ARG_IP:
                        ip = argv[i];
                        printf("FPGA IP set to %s\n", ip);
                        break;
                    case ARG_PORT:
                        port = atoi(argv[i]);
                        if(port <= 0) {
                            printf("Invalid port given, will be using the default port");
                        }
                        break;
                    case ARG_NONE:
                    default:
                        break;
                }
                expecting_value = 0;
            }
        }

        if(expecting_value) {
            printf("Did not find value for last argument...exiting\n");
            return 1;
        }
    }

    // TODO these parameters should be user settable somehow
    setup_logger(LOGGER_NAME, DEFAULT_REDIS_HOST, LOG_FILENAME,
                 verbosity_stdout, verbosity_file, verbosity_redis,
                 LOG_MESSAGE_MAX);
    the_logger->add_newlines = 1;

    // First connect to FPGAs
    for(i=0;i<NUM_XEMS;i++) {
        if(setup_udp(ip, &(XEMS[i]))) {
            daq_log(LOG_ERROR, "Cannot connect to XEM%i", xem->device_id);
        }
        else {
            daq_log(LOG_INFO, "Connected to XEM%i", xem->device_id);
        }
    }

    memset(resp_buffer, 0, BUFFER_SIZE);
    memset(command_buffer, 0, BUFFER_SIZE);
    signal(SIGINT, sig_handler);


    // Set up command_tables
    board_specific_command_table = ceres_commands;
    SAFE_READ_ADDRESS = CERES_SAFE_READ_ADDRESS;

    ServerCommand* commandTable = combine_command_tables();

    initServerConfig();
    if(port > 0) {
        server.port = port;
    }
    server_command_table = commandTable;
    initServer();

    aeSetBeforeSleepProc(server.el, beforeSleep);
    //aeSetAfterSleepProc(server.el,afterSleep);
    aeMain(server.el);
    aeDeleteEventLoop(server.el);

    daq_log(LOG_WARN,"Cntrl-C found, quitting");
    cleanup_logger();
    return 0;
}
