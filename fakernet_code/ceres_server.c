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
    struct fnet_ctrl_client* fnet_client;
    int device_id;
    const char* ip;
//    int is_up;
} XEMConn;
XEMConn XEMS[NUM_XEMS];
XEMConn* active_xem;

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


void set_active_xem_mask_command(client* c, int argc, sds* argv) {
    int i;

    int unavailable_xems[NUM_XEMS];
    int unavailable_count = 0;

    int available_xems[NUM_XEMS];
    int available_count = 0;
    uint32_t device_id_mask;

    if(argc < 2) {
        addReplyErrorFormat(c, "Must provide active xem argument");
        return;
    }

    device_id_mask = strtoul(argv[1], NULL, 0);
    if(!c->server_data) {
        c->server_data = malloc(sizeof(int));
    }

    // Unset the XEM, if the client tries to set the XEM ID to a invalid ID
    // then I want the active XEM to be un-set so any further commands don't
    // get accidentally sent to whatever XEM was last being talked to, cause
    // that'd probably be unintentional.
    *(int*)c->server_data = 0;

    // If the requested device_id_mask is zero, we're done
    if(device_id_mask == 0) {
        addReplyStatus(c, "OK");
        return;
    }

    // First count the number of set bits in the mask, if it's too high or too
    // low, then that's a problem
    int requested_count = 0;
    for(i=0; i <32; i++) {
        if(device_id_mask & (1<<i)) {
            requested_count+=1;
        }
    }

    if(requested_count > NUM_XEMS) {
        addReplyErrorFormat(c, "Too XEMs requested in active mask");
        return;
    }

    for(i=0; i<NUM_XEMS; i++) {
        if((device_id_mask & (1<<XEMS[i].device_id)) != 0) {

            if(XEMS[i].fnet_client == NULL) {
                // The server wasn't able to connect to this XEM when it booted up
                // Should not allow it to be the active XEM
                // TODO, could try and reconnect here maybe
                unavailable_xems[unavailable_count++] = XEMS[i].device_id;
            }
            else {
                available_xems[available_count++] = i;
                //*(int*)c->server_data |= i;
            }

        }
    }
    if(available_count == requested_count) {
        for(i=0; i<available_count; i++) {
            *(int*)c->server_data |= (1<<available_xems[i]);
        }
        addReplyStatus(c, "OK");
    }
    else if(unavailable_count > 0) {
        char buffer[128];
        int offset = 0;
        for(i=0; i<unavailable_count; i++) {
            snprintf(buffer +offset, 128-offset, "%i ", unavailable_xems[i]);
            offset = strlen(buffer);
        }

        addReplyErrorFormat(c, "XEM(s) %s aren't connected", buffer);
    }
    else {
        // The only way unavailable_count can be zero and available_count is
        // less than requested_count should be if the requested XEM device IDs
        // aren't in the XEMs array
        addReplyErrorFormat(c, "One or more requested XEMs isn't valid");
    }
}

void get_active_xem_mask_command(client* c, int argc, sds* argv) {
    UNUSED(argc);
    UNUSED(argv);
    int i;
    int device_index_mask;
    uint32_t device_id_mask = 0;

    if(!c->server_data || *(int*)c->server_data <= 0) {
        // RESP NULL response is "$-1\r\n"
        addReplyLongLongWithPrefix(c, -1, '$');
    }
    else {
        device_index_mask = *(int*)c->server_data;
        for(i=0;i<NUM_XEMS;i++) {
            if(((1<<i) & device_index_mask) == 0) {
                continue;
            }
            device_id_mask |= (1<<XEMS[i].device_id);

        }

        addReplyLongLong(c, device_id_mask);
    }
}

void get_available_xems_command(client* c, int argc, sds* argv) {
    UNUSED(argc);
    UNUSED(argv);
    int i;
    int count = 0;
    for(i=0; i<NUM_XEMS; i++) {
        if(XEMS[i].fnet_client) {
            count+=1;
        }
    }

    if(count == 0) {
        addReplyLongLongWithPrefix(c, -1, '$');
        return;
    }

    addReplyLongLongWithPrefix(c, count, '*');
    for(i=0; i<NUM_XEMS; i++) {
        if(XEMS[i].fnet_client) {
            addReplyLongLong(c, XEMS[i].device_id);
        }
    }
}

int setup_udp(XEMConn* xem) {
    int reliable = 0; // wtf does this do?
    const char* err_string = NULL;
    if(dummy_mode) {
        return 0;
    }

    int loop_count = 0;
    do {
        xem->fnet_client = fnet_ctrl_connect(xem->ip, reliable, &err_string, NULL);
        usleep(10000);
    }while(!xem->fnet_client && loop_count++ < 10);

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

    fnet_ctrl_get_send_recv_bufs(active_xem->fnet_client, &send, &recv);

    addr = base + addr;
    addr &= 0x3FFFFFF; // Only the first 25-bits are valid

    send[0].data = htonl(0x0);
    send[0].addr = htonl(FAKERNET_REG_ACCESS_ADDR_READ | addr);

    int num_items = 1;
    int ret = fnet_ctrl_send_recv_regacc(active_xem->fnet_client, num_items);

    // Pretty sure "ret" will be the number of UDP reg-accs dones
    if(ret <= 0) {
        printf("ERROR %i\n", ret);
        char* last_error = fnet_ctrl_last_error(active_xem->fnet_client);
        if(last_error) {
            printf("%s\n", last_error);
        }
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

    if(read_addr(base, addr, result)) {
        return -1;
    }
    usleep(100);
    return read_addr(SAFE_READ_ADDRESS, 0x0, result);
}

int write_addr(uint32_t base, uint32_t addr, uint32_t data) {
      fakernet_reg_acc_item *send;
      fakernet_reg_acc_item *recv;

      if(dummy_mode) { return 0; }

      addr = addr+base;
      fnet_ctrl_get_send_recv_bufs(active_xem->fnet_client, &send, &recv);

      addr &= 0x3FFFFFF; // Only the first 25-bits are valid

      send[0].data = htonl(data);
      send[0].addr = htonl(FAKERNET_REG_ACCESS_ADDR_WRITE | addr);

      int num_items = 1;
      int ret = fnet_ctrl_send_recv_regacc(active_xem->fnet_client, num_items);
      
      // Pretty sure "ret" will be the number of UDP reg-accs dones
      if(ret == 0) {
          printf("ERROR %i\n", ret);
          char* last_error = fnet_ctrl_last_error(active_xem->fnet_client);
          if(last_error) {
              printf("%s\n", last_error);
          }
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
    {"set_active_xem_mask", set_active_xem_mask_command, NULL, 2, 0, 0, 0},
    {"get_active_xem_mask", get_active_xem_mask_command, NULL, 1, 0, 0, 0},
    {"get_available_xems", get_available_xems_command , NULL, 1, 0, 0, 0},
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
    ARG_PORT
};

void print_help_message() {
    printf("You need help\n");
}

void ceres_call(client *c) {
    int i;
    ServerCommand *real_cmd = c->cmd;

    // Need to set the active XEM to the current client's specified one.
    // If the active_xem isn't set then the only command that can be executed is to set the active xem
        if(real_cmd->func == set_active_xem_mask_command) {
            set_active_xem_mask_command(c, c->argc, c->argv);
            return;
        }
        else if (real_cmd->func == send_command_table) {
            send_command_table(c, c->argc, c->argv);
            return;
        }
        else if (real_cmd->func == get_active_xem_mask_command) {
            get_active_xem_mask_command(c, c->argc, c->argv);
            return;
        }
        else if (real_cmd->func == get_available_xems_command) {
            get_available_xems_command(c, c->argc, c->argv);
            return;
        }

        if(!c->server_data || *(int*)c->server_data < 0) {
            addReplyErrorFormat(c, "Cannot perform command until XEM ID is set");
            return;
        }

    // If here then the client's active xem is set, so make that the server's active xem
    //int xem_index = *(int*)c->server_data;
    int xem_mask = *(int*)c->server_data;
    int ixem;
    int active_xem_count = 0;
    // First count how many active XEMs there are
    for(ixem=0; ixem<NUM_XEMS; ixem++) {
        if(((1<<ixem) & xem_mask) == 0) {
            continue;
        }
        active_xem_count+=1;
    }

    addReplyLongLongWithPrefix(c, active_xem_count, '*');
    for(ixem=0; ixem<NUM_XEMS; ixem++) {
        if(((1<<ixem) & xem_mask) == 0) {
            continue;
        }
        active_xem = &XEMS[ixem];

        /* Call the command. */
        if(real_cmd->func) {
            c->cmd->func(c, c->argc, c->argv);
        }
        else {
            // Use legacy_func
            int num_ints_needed = real_cmd->nargs > real_cmd->nresp ? real_cmd->nargs : real_cmd->nresp;
            uint32_t* args_uint = malloc(sizeof(uint32_t)*num_ints_needed);
            for(i=1; i<real_cmd->nargs; i++) {
                args_uint[i-1] = strtoul(c->argv[i], NULL, 0);
            }
            uint32_t resp = real_cmd->legacy_func(args_uint);
            if(real_cmd->nresp == 0) {
                addReplyStatus(c, "OK");
            }
            else if(real_cmd->nresp == 1) {
                addReplyLongLong(c, (long long)resp);
            }
            else {
                if(resp != 0) {
                    // TODO! need to add low level error string!
                    addReplyErrorFormat(c, "Error performing command '%s'", real_cmd->name);
                }
                else {
                    // RESP array is *N\r\n where N is the length of the array, followed
                    // by the elements of the array
                    addReplyLongLongWithPrefix(c, (long long)real_cmd->nresp, '*');
                    for(i=0; i<real_cmd->nresp; i++) {
                        addReplyLongLong(c, (long long)args_uint[i]);
                    }
                }
            }
            free(args_uint);
        }
    }

    // Unset the server's active xem
    active_xem = NULL;
}

int main(int argc, char** argv) {
    // TODO figure out how to un-hardcode these
    XEMS[0].fnet_client = NULL; XEMS[0].device_id = 4; XEMS[0].ip = "192.168.84.196";
    XEMS[1].fnet_client = NULL; XEMS[1].device_id = 5; XEMS[1].ip = "192.168.84.197";
    XEMS[2].fnet_client = NULL; XEMS[2].device_id = 6; XEMS[2].ip = "192.168.84.198";
    XEMS[3].fnet_client = NULL; XEMS[3].device_id = 7; XEMS[3].ip = "192.168.84.199";
    XEMS[4].fnet_client = NULL; XEMS[4].device_id = 8; XEMS[4].ip = "192.168.84.200";
    XEMS[5].fnet_client = NULL; XEMS[5].device_id = 9; XEMS[5].ip = "192.168.84.201";
    XEMS[6].fnet_client = NULL; XEMS[6].device_id = 10; XEMS[6].ip = "192.168.84.202";
    XEMS[7].fnet_client = NULL; XEMS[7].device_id = 11; XEMS[7].ip = "192.168.84.203";

    int port = -1;
    int i;
    if(argc > 1 ) {
        enum ArgIDs expecting_value = 0;
        for(i=1; i < argc; i++) {
            if(!expecting_value) {
                if(strcmp(argv[i], "--port") == 0) {
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
        XEMConn* xem = &XEMS[i];
        if(setup_udp(xem)) {
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

    serverSetCustomCall(ceres_call);
    aeSetBeforeSleepProc(server.el, beforeSleep);
    //aeSetAfterSleepProc(server.el,afterSleep);
    aeMain(server.el);
    aeDeleteEventLoop(server.el);

    daq_log(LOG_WARN,"Cntrl-C found, quitting");
    cleanup_logger();
    return 0;
}
