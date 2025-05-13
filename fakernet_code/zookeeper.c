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
#include <wait.h>
#include "server_common.h"
#include "server.h"
#include "util.h"
#include "daq_logger.h"

#include "data_builder.h"

// For doing "double" reads the 2nd read should be from this register
#define BUFFER_SIZE 2048
#define DEFAULT_IP  "192.168.84.192"
#define PIPE_BUFFER_SIZE 128

#define LOGGER_NAME "kintex_server"
#define DEFAULT_REDIS_HOST "127.0.0.1"
#define LOG_FILENAME "kintex_server.log"
#define LOG_MESSAGE_MAX 1024

int verbosity_stdout = LOG_INFO;
int verbosity_file = LOG_INFO;
int verbosity_redis = LOG_WARN;


int dummy_mode = 0;
char command_buffer[BUFFER_SIZE];
char resp_buffer[BUFFER_SIZE];
static volatile int end_main_loop = 0;
int start_data_builder = -1;

typedef struct IPC_Pipe {
    int device_index;
    int child_pid;
    int parent_pid;
    int p2c_pipe[2]; // Parent to child pipe
    int c2p_pipe[2]; // Child to parent pipe
    void* buffer[PIPE_BUFFER_SIZE];
} IPC_Pipe;
IPC_Pipe pipes[32];
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
    close(write_pipe);
    close(read_pipe);

    pipe->p2c_pipe[0] = -1;
    pipe->p2c_pipe[1] = -1;
    pipe->c2p_pipe[0] = -1;
    pipe->c2p_pipe[1] = -1;
}

void child_read_proc(aeEventLoop* el, int fd, void* client_data, int mask) {
    (void) el; // Unused
    (void) mask; // Unused (I'm also not sure how to use it lol)
    IPC_Pipe* this_pipe = (IPC_Pipe*) client_data;

    int nbyte = read(fd,  this_pipe->buffer, PIPE_BUFFER_SIZE);
    if(nbyte == 0) {
        // Indicates the end of the file
        // Indicates the child process closed its end of the pipe
        // That should mean that the child process is finished

        // TODO if this ever gets called and the child did not actually exit
        // that will cause this to hang. Perhaps I should set the "no hang" option
        // in waitpid or something, then maybe call "kill"
        waitpid(this_pipe->child_pid, NULL, 0);
        clean_up_child_process_pipes(this_pipe);
    }
    else if(nbyte < 0) {
        printf("Read error: %s\n", strerror(errno));
    }
    printf("Nbyte %i %c\n", nbyte, ((char*)this_pipe->buffer)[0]);
}

void start_builder_command(client* c, int argc, sds* argv) {
    (void) argv; // Unused
    (void) argc; // Unused

    unsigned long device_id = strtoul(c->argv[1], NULL, 0);
    if(device_id >= 32) {
        addReplyErrorFormat(c, "Device ID %lu is not valid.", device_id);
        return;
    }

    pipe(pipes[device_id].p2c_pipe);
    pipe(pipes[device_id].c2p_pipe);
    pipes[device_id].parent_pid = getpid();
    pipes[device_id].device_index = device_id;
    // The process is already open & running (presumably)
    if(pipes[device_id].child_pid) {
        addReplyLongLong(c, (long long) -1);
        return;
    }
    pid_t child_pid = fork();
    if(!child_pid) {
        // Child process
        printf("Starting Data Builder %i\n", device_id);
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
    // Block until I receive some signal from the child process
    // (TODO, I should figure out how to block the client until I get a signal
    // from the pipe or whatever, instead of just sitting here waiting
    aeCreateFileEvent(server.el, pipes[device_id].c2p_pipe[0], AE_READABLE, child_read_proc, (void*)&(pipes[device_id]));
    //TODO I probably do need to create a 
    //aeCreateFileEvent(server.el, pipes[device_id].p2c_pipe[1], AE_WRITABLE, child_read_proc, (void*)&(pipes[device_id]));

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
    addReplyStatus(c, "OK");
}

void is_builder_reeling_command(client* c, int argc, sds* argv) {
    //TODO
    addReplyLongLong(c, 0);
}

void read_active_builders_command(client* c, int argc, sds* argv) {
    //TODO
    addReplyLongLong(c, 0);
}

void reset_builder_connection_command(client* c, int argc, sds* argv) {
    //TODO
    addReplyLongLong(c, 0);
}

static ServerCommand default_commands[] = {
    {"start_builder", start_builder_command, NULL, 2, 1, 0, 0},
    {"stop_builder", stop_builder_command, NULL, 2, 1, 0, 0},
    {"is_builder_reeling", is_builder_reeling_command, NULL, 2, 1, 0, 0},
    {"reset_builder_connection" reset_builder_connection_command, NULL, 2, 1, 0, 0},
    {"read_active_builders", read_active_builders_command, NULL, 1, 1, 0, 0},
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


// For arguements with a values
enum ArgIDs {
    ARG_NONE=0,
    ARG_IP,
    ARG_PORT
};

void print_help_message() {
    printf("You need help\n");
}

int server_main(int argc, char** argv) {
    const char* ip = DEFAULT_IP;
    int port = -1;
    int i;
    printf("%i \n", argc);
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


    memset(resp_buffer, 0, BUFFER_SIZE);
    memset(command_buffer, 0, BUFFER_SIZE);
    signal(SIGINT, sig_handler);


    initServerConfig();
    if(port > 0) {
        server.port = port;
    }
    server_command_table = default_commands;
    initServer();

    aeSetBeforeSleepProc(server.el, beforeSleep);
    //aeSetAfterSleepProc(server.el,afterSleep);
    aeMain(server.el);
    aeDeleteEventLoop(server.el);

    daq_log(LOG_WARN,"Cntrl-C found, quitting");
    cleanup_logger();
    return 0;
}

int main(int argc, char** argv) {

    int i;

    server_main(argc-1, argv+1);

    if(start_data_builder >= 0) {
        printf("CHILD %i\n", start_data_builder);
        data_builder_main(start_data_builder);
        while(1) {
            printf("YOINK");
            write(pipes[start_data_builder].c2p_pipe[WRITE_PIPE_IDX], "TEST", 5);
            sleep(10);
        }
        printf("CHILD DONE\n");
    }
    else {
        // Kill all the child processes that are around.
        int status;
        for(i =0; i<32; i++) {
            if(pipes[i].child_pid > 0) {
                kill(pipes[i].child_pid, SIGKILL);
                waitpid(pipes[i].child_pid, &status, 0);
                //clean_up_child_process_pipes(&pipes[i])
            }
        }
    }
    return 0;
}
