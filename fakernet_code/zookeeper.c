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
#include <sys/wait.h>
#include <getopt.h>
#include "server_common.h"
#include "server.h"
#include "daq_logger.h"

#include "data_builder.h"

#define BUFFER_SIZE 2048
#define DEFAULT_IP  "192.168.84.192"
#define PIPE_BUFFER_SIZE 128

#define LOGGER_NAME "zoo_keeper"
#define DEFAULT_REDIS_HOST "127.0.0.1"
#define LOG_FILENAME "zoo_keeper_server.log"
#define LOG_MESSAGE_MAX 1024

int verbosity_stdout = LOG_INFO;
int verbosity_file = LOG_INFO;
int verbosity_redis = LOG_WARN;

int dummy_mode = 0;
char command_buffer[BUFFER_SIZE];
char resp_buffer[BUFFER_SIZE];
static volatile int end_main_loop = 0;
int start_data_builder = -1;

// Linked list for keeping of track of IO commands that have been requested and
// the client that requested them.
typedef struct ManagerIOList {
    ManagerIO io_cmd; // The actual request
    client* client; // The client that should receive a response
    struct ManagerIOList* next; // The next command to process
} ManagerIOList;

#define MAX_NUM_BUILDERS 32
typedef struct IPC_Pipe {
    int device_index;
    int child_pid;
    int parent_pid;
    int p2c_pipe[2]; // Parent to child pipe
    int c2p_pipe[2]; // Child to parent pipe
    ManagerIOList* cmd_list;
    void* buffer[PIPE_BUFFER_SIZE];
} IPC_Pipe;
IPC_Pipe pipes[MAX_NUM_BUILDERS];
#define READ_PIPE_IDX 0
#define WRITE_PIPE_IDX 1

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
        server.el->stop=1;
        return;
    }
    else if(end_main_loop == 1) {
        server.shutdown_asap = 1;
        end_main_loop = 2;
    }
    exit(0);
}

void clean_up_child_process_pipes(IPC_Pipe* pipe) {
    pipe->device_index = -1;
    pipe->child_pid = 0;
    pipe->parent_pid = 0;
    int write_pipe = pipe->p2c_pipe[WRITE_PIPE_IDX];
    int read_pipe = pipe->c2p_pipe[READ_PIPE_IDX];
    aeDeleteFileEvent(server.el, read_pipe, AE_READABLE);
    aeDeleteFileEvent(server.el, write_pipe, AE_WRITABLE);

    // If there's any remaining commands that haven't been processed,
    // remove them, and send an error to their client.
    while(pipe->cmd_list) {
        ManagerIOList* this_cmd = pipe->cmd_list;
        client *c = this_cmd->client;
        if(c) {
            addReplyError(c, "Builder process was stopped");
            unblockClient(c);
        }
        pipe->cmd_list = this_cmd->next;
        free(this_cmd);
    }

    close(write_pipe);
    close(read_pipe);

    pipe->p2c_pipe[0] = -1;
    pipe->p2c_pipe[1] = -1;
    pipe->c2p_pipe[0] = -1;
    pipe->c2p_pipe[1] = -1;
}

// Low level function to write the command to the pipe
ssize_t send_manager_command_now(int fd, ManagerIO cmd) {
    ssize_t nbytes = 0;
    do {
        nbytes += write(fd, ((char*)&cmd)+nbytes, sizeof(cmd)-nbytes);
    } while(nbytes > 0 && nbytes < (ssize_t)sizeof(ManagerIO));
    // TODO handle errors!
    return nbytes;
}

// This process handles receive data from any of the fork'd child processes.
// In general data should be received whenver there was a command set to request
// data. But if the child process dies this process will get run with "read" returning
// a zero bytes.
void child_read_proc(aeEventLoop* el, int fd, void* client_data, int mask) {
    (void) el; // Unused
    (void) mask; // Unused (I'm also not sure how to use it lol)
    IPC_Pipe* this_pipe = (IPC_Pipe*) client_data;

    ManagerIOList* this_cmd = this_pipe->cmd_list;
    if(this_cmd == NULL) {
        // Not sure why this would happen...
        // Could happen if the child process crashes TODO (figure out how to handles this)
        daq_log(LOG_ERROR,"Unexpected circumstance found. Data received for child proc that isn't expected.");
    }

    ManagerIO recv_cmd;
    ssize_t nbyte = 0;
    do {
        nbyte += read(fd,  ((void*)&recv_cmd)+nbyte, sizeof(ManagerIO)-nbyte);
    } while(nbyte > 0 && nbyte < (ssize_t)sizeof(ManagerIO));

    if(nbyte == 0) {
        // Indicates the end of the file
        // Indicates the child process closed its end of the pipe
        // That should mean that the child process is finished

        // TODO if this ever gets called and the child did not actually exit
        // that will cause this to hang. Perhaps I should set the "no hang" option
        // in waitpid or something, then maybe call "kill"
        kill(this_pipe->child_pid, SIGKILL);
        waitpid(this_pipe->child_pid, NULL, 0);
        clean_up_child_process_pipes(this_pipe);
        return;
    }
    else if(nbyte < 0) {
        // I have no idea why this would happen.
        daq_log(LOG_ERROR, "Read error: %s\n", strerror(errno));
    }

    // TODO should check the received command matches the one speciefied in the queue

    client* c = this_cmd->client;
    // If 'c' is NULL it indicates the client disconnected.
    if(c) {
        // TODO could improve this by having a callback get called instead of just replying this way
        addReplyLongLong(c, recv_cmd.arg);
        unblockClient(c);
    }

    // Finally, remove the top of the command list and free it.
    // The send the next command in the list
    this_pipe->cmd_list = this_cmd->next;
    free(this_cmd);

    // Send the next request to the child process (if there are any)
    if(this_pipe->cmd_list) {
        send_manager_command_now(this_pipe->p2c_pipe[WRITE_PIPE_IDX], this_pipe->cmd_list->io_cmd);
        // TODO, in principle the child process could have crashed/died between
        // the above read and now. I need to check for errors here.
        // From testing it works out okay so maybe I don't need to do anything
    }
}

// Either send the command via the child process pipe,
// or append the command to the list of commands
void send_manager_command_async(client* c, IPC_Pipe* pipe, ManagerIO cmd) {
    ManagerIOList * this_cmd = malloc(sizeof(ManagerIOList));

    this_cmd->io_cmd = cmd;
    this_cmd->client = c;
    this_cmd->next = NULL;

    // If the command list is empty, add this command to the list, then send the
    // command to the child process
    if(!pipe->cmd_list) {
        pipe->cmd_list = this_cmd;
        send_manager_command_now(pipe->p2c_pipe[WRITE_PIPE_IDX], cmd);
    }
    else {
        // If the command list is not empty, add it to the list.
        ManagerIOList* this_node = pipe->cmd_list;
        // Search for the end of the list
        while(this_node->next) {
            this_node = this_node->next;
        }
        this_node->next = this_cmd;
    }
}

void start_builder_command(client* c, int argc, sds* argv) {
    (void) argc; // Unused

    unsigned long device_id = strtoul(argv[1], NULL, 0);
    if(device_id >= MAX_NUM_BUILDERS) {
        addReplyErrorFormat(c, "Device ID %lu is not valid.", device_id);
        return;
    }

    // The process is already open & running (presumably)
    if(pipes[device_id].child_pid) {
        addReplyLongLong(c, (long long) -1);
        return;
    }
    pipe(pipes[device_id].p2c_pipe);
    pipe(pipes[device_id].c2p_pipe);
    pipes[device_id].parent_pid = getpid();
    pipes[device_id].device_index = device_id;
    pid_t child_pid = fork();
    if(!child_pid) {
        // Child process
        daq_log(LOG_WARN, "Starting Data Builder %lu, PID=%i\n", device_id, getpid());
        cleanup_logger();
        close(STDOUT_FILENO); // Get rid of printf output
        server.el->stop = 1;
        close(pipes[device_id].c2p_pipe[READ_PIPE_IDX]);
        close(pipes[device_id].p2c_pipe[WRITE_PIPE_IDX]);
        pipes[device_id].child_pid = getpid();
        start_data_builder = device_id;
        return;
    }

    // Parent process
    pipes[device_id].child_pid = child_pid;
    close(pipes[device_id].c2p_pipe[WRITE_PIPE_IDX]);
    close(pipes[device_id].p2c_pipe[READ_PIPE_IDX]);
    // Once the child process is running, create a file event that will respond
    // whenever the child sends data from it's end of the pipe.
    aeCreateFileEvent(server.el, pipes[device_id].c2p_pipe[0], AE_READABLE, child_read_proc, (void*)&(pipes[device_id]));
    addReplyStatus(c, "OK");
}

void stop_builder_command(client* c, int argc, sds* argv) {
    (void) argv; // Unused
    (void) argc; // Unused

    int status;
    unsigned long device_id = strtoul(c->argv[1], NULL, 0);
    if(device_id >= 32) {
        addReplyErrorFormat(c, "Device ID %lu is not valid.", device_id);
        return;
    }

    // First check if the child process actually exists
    if(!pipes[device_id].child_pid) {
        addReplyLongLong(c, (long long) -1);
        return;
    }

    // TODO, I want to send "SIGTERM" so the child process exits on its own,
    // but I also need to send SIGKILL if it hasn't exited after a short amount
    // of time. Should perhaps create a time event to check & kill the process
    //kill(pipes[device_id].child_pid, SIGKILL);
    kill(pipes[device_id].child_pid, SIGTERM);
    // TODO, should somehow make sure waitpid never hangs
    waitpid(pipes[device_id].child_pid, &status, 0);
    clean_up_child_process_pipes(&pipes[device_id]);
    addReplyStatus(c, "OK");
}

void clean_up_disconnected_client(client* c, void* data) {
    if(!data) {
        // This should never happen!
    }
    int i;
    for(i=0; i< MAX_NUM_BUILDERS; i++) {
        if(!pipes[i].child_pid) {
            continue;
        }
        // Iterate through each command, for each command if it's client is the
        // client who disconnected, then bascially get rid of that command.
        // Currently, a single client can only have a single command requested
        // at a time, so if I find one, then I should looking. But maybe someday
        // I'll add a way to have multiple commands requested at once, so I may
        // as well iterate through everything. I don't expect this will ever be
        // too computationally expensive.
        ManagerIOList* this_list = pipes[i].cmd_list;
        while(this_list) {
            if(pipes[i].cmd_list->client->id == c->id) {
                // If here then we've found a command that was requested by our
                // client that we need to get rid of. But the correct way to
                // get rid of a request is a little tricky. The correct way
                // would be to remove any request that isn't the 'top' of the
                // list, and invalidate a request that is at the top. The
                // reason is that if it's at the top then the request has been
                // sent to the child process already. So I need to leave the
                // command on the list so that when the response comes back I
                // don't get confused about where the response came from.  So
                // long story short I just set the client to NULL no matter
                // what, then when the response comes back from the child proc
                // it'll get handled like normal, just not responded too.
                // Potentially a waste of resources cause commands that no one
                // will ever hear get sent, but that's not a big deal probably.
                pipes[i].cmd_list->client = NULL;
            }
            this_list = this_list->next;
        }
    }

}

void get_active_builders_command(client* c, int argc, sds* argv) {
    UNUSED(argc);
    UNUSED(argv);
    unsigned int builder_mask = 0x0;
    int i;
    for(i=0; i<MAX_NUM_BUILDERS; i++) {
        builder_mask |= pipes[i].child_pid ? (1<<i) : 0;
    }
    addReplyLongLong(c, builder_mask);
}

// Convenience function for sending a command to a child process
void builder_send_command_generic(client *c, IPC_Pipe* pipe, ManagerIO cmd) {
    if(!pipe->child_pid) {
        // Indicates that the builder for the specified ID is not running
        addReplyError(c, "Requested builder is not running");
        return;
    }

    send_manager_command_async(c, pipe, cmd);
    blockClient(c, pipe, clean_up_disconnected_client);
}

void reset_builder_connection_command(client* c, int argc, sds* argv) {
    UNUSED(argc);
    ManagerIO cmd;
    cmd.command = CMD_RESET_CONN;
    unsigned long device_id = strtoul(argv[1], NULL, 0);
    if(device_id >= 32) {
        addReplyErrorFormat(c, "Device ID %lu is not valid.", device_id);
        return;
    }
    builder_send_command_generic(c, &(pipes[device_id]), cmd);
}

void display_headers_command(client* c, int argc, sds* argv) {
    UNUSED(argc);
    ManagerIO cmd;
    cmd.command = CMD_DISPLAY_HEADERS;
    unsigned long device_id = strtoul(argv[1], NULL, 0);
    if(device_id >= 32) {
        addReplyErrorFormat(c, "Device ID %lu is not valid.", device_id);
        return;
    }
    cmd.arg = strtoul(argv[2], NULL, 0);
    builder_send_command_generic(c, &(pipes[device_id]), cmd);
}

void get_num_built_command(client* c, int argc, sds* argv) {
    UNUSED(argc);
    ManagerIO cmd;
    unsigned long device_id = strtoul(argv[1], NULL, 0);
    if(device_id >= 32) {
        addReplyErrorFormat(c, "Device ID %lu is not valid.", device_id);
        return;
    }

    cmd.command = CMD_NUMBUILT;
    builder_send_command_generic(c, &(pipes[device_id]), cmd);
}

void is_builder_reeling_command(client* c, int argc, sds* argv) {
    UNUSED(argc);
    ManagerIO cmd;
    unsigned long device_id = strtoul(argv[1], NULL, 0);
    if(device_id >= 32) {
        addReplyErrorFormat(c, "Device ID %lu is not valid.", device_id);
        return;
    }

    cmd.command = CMD_ISREELING;
    builder_send_command_generic(c, &(pipes[device_id]), cmd);
}

void get_builder_pid_command(client* c, int argc, sds* argv) {
    UNUSED(argc);
    unsigned long device_id = strtoul(argv[1], NULL, 0);
    if(device_id >= 32) {
        addReplyErrorFormat(c, "Device ID %lu is not valid.", device_id);
    }
    else if(pipes[device_id].child_pid <= 0) {
        addReplyError(c, "Requested builder is not running");
    }
    else {
        addReplyLongLong(c, pipes[device_id].child_pid);
    }
}

static ServerCommand commandTable[] = {
    {"start_builder", start_builder_command, NULL, 2, 1, 0, 0},
    {"stop_builder", stop_builder_command, NULL, 2, 1, 0, 0},
    {"is_builder_reeling", is_builder_reeling_command, NULL, 2, 1, 0, 0},
    {"reset_builder_connection", reset_builder_connection_command, NULL, 2, 1, 0, 0},
    {"set_display_headers", display_headers_command, NULL, 3, 1, 0, 0},
    {"get_num_built", get_num_built_command, NULL, 2, 1, 0, 0},
    {"get_builder_pid", get_builder_pid_command, NULL, 2, 1, 0, 0},
    {"get_active_builders", get_active_builders_command, NULL, 1, 1, 0, 0},
    {"", NULL, NULL, 0, 0, 0, 0} // Must be last
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

int server_main(int port) {

    // TODO these parameters should be user settable somehow
    setup_logger(LOGGER_NAME, DEFAULT_REDIS_HOST, LOG_FILENAME,
                 verbosity_stdout, verbosity_file, verbosity_redis,
                 LOG_MESSAGE_MAX);
    the_logger->add_newlines = 1;


    memset(resp_buffer, 0, BUFFER_SIZE);
    memset(command_buffer, 0, BUFFER_SIZE);
    signal(SIGINT, sig_handler);


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

void print_help_message(void) {
    printf("zookeeper: runs a server that allows clients to request data builders to be started/stopped and provides monitoring.\n"
            "\tusage: zookeeper [--port port] [--help]\n"
            "\targuments:\n"
            "\t--port -p\tPort for server to listen to connections on.\n");
}

int main(int argc, char** argv) {

    int i;
    int port = -1; // -1 will go with the default option
    int dry_run = 0;
    struct option clargs[] = {
        {"port", required_argument, NULL, 'p'},
        {"dry-run", no_argument, NULL, 'd'},
        //{"verbose", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        { 0, 0, 0, 0}};

    int optindex;
    int opt;
    while( (opt = getopt_long(argc, argv, "p:dh", clargs, &optindex)) != -1)  {
        switch(opt) {
            case 'p':
                port = strtoul(optarg, NULL, 0);
                break;
            case 'd':
                dry_run = 1;
                break;
            case 'h':
            default:
                print_help_message();
                return 0; // exit the program
        }
    }
    printf("Starting server, listening on port %i\n", port);
    server_main(port);

    if(start_data_builder >= 0) {
        struct BuilderConfig the_config = default_builder_config();

        int builder_id = start_data_builder;
        char* ip_addr_buffer = malloc(32);
        char* log_filename_buffer = malloc(64);
        char* log_name = malloc(64);
        snprintf(ip_addr_buffer, 32, "192.168.84.%i", 192+builder_id);
        if(builder_id == 0) {
            snprintf(log_name, 64, "fontus_data_builder");
        }
        else {
            snprintf(log_name, 64, "ceres_data_builder_%i", builder_id);
        }
        the_config.ip = ip_addr_buffer;
        the_config.log_name = log_name;
        // If the device ID is zero this should be a FONTUS data builder,
        // otherwise it should be a CERES data builder.
        the_config.ceres_builder = builder_id != 0;

        if(dry_run) {
            the_config.dry_run = 1;
            the_config.ip = "127.0.0.1";
        }

        snprintf(log_filename_buffer, 64, "zookeeper_data_builder_%i.log", builder_id);
        the_config.error_filename = log_filename_buffer;
        //config.redis_host = DEFAULT_REDIS_HOST;
        the_config.in_pipe = pipes[builder_id].p2c_pipe[READ_PIPE_IDX];
        the_config.out_pipe = pipes[builder_id].c2p_pipe[WRITE_PIPE_IDX];

        data_builder_main(the_config);

        free(ip_addr_buffer);
        free(log_filename_buffer);
        free(log_name);
    }
    else {
        // Kill all the child processes that are around.
        int status;
        for(i =0; i<MAX_NUM_BUILDERS; i++) {
            if(pipes[i].child_pid > 0) {
                kill(pipes[i].child_pid, SIGKILL);
                waitpid(pipes[i].child_pid, &status, 0);
                //clean_up_child_process_pipes(&pipes[i])
            }
        }
    }
    return 0;
}
