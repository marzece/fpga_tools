#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h> // TODO Remove this once _serverPanic & _serverAssert are removed
#include <stdio.h> // TODO Remove this once _serverPanic & _serverAssert are removed

#include "server_common.h"
#include "server.h"
#include "ae.h"
#include "anet.h"


/* Output buffer limits presets. */
clientBufferLimitsConfig clientBufferLimitsDefaults = {0, 0, 0}; /* normal */


struct Server server;
ServerCommand* server_command_table;
ServerCommand send_command_table_command = {"send_command_table", send_command_table, NULL, 1, 0, 0, 0};

// stolen from redis/server.c
void daemonize(void) {
    int fd;

    if (fork() != 0) exit(0); /* parent exits */
    setsid(); /* create a new session */

    /* Every output goes to /dev/null. If Redis is daemonized but
     * the 'logfile' is set to 'stdout' in the configuration file
     * it will not log at all. */
    if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
}

// TODO! these assert & panic functions should be implemented by the "final" program.
// Not here, I.e. the actual DAQ server program with the 'int main' should define these functions!
void _serverAssert(const char *estr, const char *file, int line) {
    fprintf(stderr, "=== ASSERTION FAILED ===");
    fprintf(stderr, "==> %s:%d '%s' is not true",file,line,estr);
    *((char*)-1) = 'x';
}

void _serverPanic(const char *file, int line, const char *msg, ...) {
    va_list ap;
    va_start(ap,msg);
    char fmtmsg[256];
    vsnprintf(fmtmsg,sizeof(fmtmsg),msg,ap);
    va_end(ap);

    serverLog(LL_WARNING,"------------------------------------------------");
    serverLog(LL_WARNING,"!!! Software Failure. Press left mouse button to continue");
    serverLog(LL_WARNING,"Guru Meditation: %s #%s:%d",fmtmsg,file,line);
    serverLog(LL_WARNING,"------------------------------------------------");
    *((char*)-1) = 'x';
}

void send_command_table(client* c, int argc, sds* argv) {
    UNUSED(argc);
    UNUSED(argv);
    int i;
    int ncommands;
    ServerCommand* cmd = server_command_table;
    // First need to count the commands available
    while(cmd->func || cmd->legacy_func) {
        cmd++;
    }
    ncommands = cmd - server_command_table;

    addReplyLongLongWithPrefix(c, ncommands, '*');
    for(i=0; i<ncommands; i++) {
        cmd = &(server_command_table[i]);
        addReplyStatusFormat(c, "%s %i", cmd->name, cmd->nargs-1);
    }

}

void initServerConfig(void) {

   // server.runid[CONFIG_RUN_ID_SIZE] = '\0';
   // server.timezone = getTimeZone(); /* Initialized by tzset(). */
    server.hz = server.config_hz = CONFIG_DEFAULT_HZ;
    server.dynamic_hz = CONFIG_DEFAULT_DYNAMIC_HZ;
    server.arch_bits = (sizeof(long) == 8) ? 64 : 32;
    server.port = CONFIG_DEFAULT_SERVER_PORT;
    server.bindaddr_count = 0;
    server.maxclients = CONFIG_DEFAULT_MAX_CLIENTS;
    server.verbosity = CONFIG_DEFAULT_VERBOSITY;
    server.ipfd_count = 0;
    //server.reserved_fds = 10;
    //server.unixsocket = NULL;
    //server.unixsocketperm = CONFIG_DEFAULT_UNIX_SOCKET_PERM;
    //server.maxidletime = CONFIG_DEFAULT_CLIENT_TIMEOUT;
    //server.tcpkeepalive = CONFIG_DEFAULT_TCP_KEEPALIVE;
    //server.active_expire_enabled = 1;
    server.proto_max_bulk_len = CONFIG_DEFAULT_PROTO_MAX_BULK_LEN;
    server.client_max_querybuf_len = PROTO_MAX_QUERYBUF_LEN;
    //server.daemonize = CONFIG_DEFAULT_DAEMONIZE;
    //server.blocked_clients = 0;
    //memset(server.blocked_clients_by_type,0, sizeof(server.blocked_clients_by_type));
    server.shutdown_asap = 0;

    /* Client output buffer limits */
    // -- Eric M. client_obuf_limits used to be an array with a set of limits for each "type" of client.
    // Should I ever try and re-add pubsub connections I'll need to make this an array again
    server.client_obuf_limits = clientBufferLimitsDefaults;
}

/* This is our timer interrupt, called server.hz times per second.
 * Here is where we do a number of things that need to be done asynchronously.
 *
 * Everything directly called here will be called server.hz times per second,
 * so in order to throttle execution of things we want to do less frequently
 * a macro is used: run_with_period(milliseconds) { .... } */
int serverCron(struct aeEventLoop* eventLoop, long long id, void *clientData) {
    UNUSED(eventLoop);
    UNUSED(id);
    UNUSED(clientData);

    /* Update the time cache. */
    updateCachedTime(1);

    // TODO re-add dynamic rate?
    //server.hz = server.config_hz;
    /* Adapt the server.hz value to the number of configured clients. If we have
     * many clients, we want to call serverCron() with an higher frequency. */
    //if (server.dynamic_hz) {
    //    while (listLength(server.clients) / server.hz >
    //           MAX_CLIENTS_PER_CLOCK_TICK)
    //    {
    //        server.hz *= 2;
    //        if (server.hz > CONFIG_MAX_HZ) {
    //            server.hz = CONFIG_MAX_HZ;
    //            break;
    //        }
    //    }
    //}




    /* We received a SIGTERM, shutting down here in a safe way, as it is
     * not ok doing so inside the signal handler. */
    if (server.shutdown_asap) {
        if (prepareForShutdown() == C_OK) {
            exit(0);
        }
        serverLog(LL_WARNING,"SIGTERM received but errors trying to shut down the server, check the logs for more information");
        server.shutdown_asap = 0;
    }



    /* We need to do a few operations on clients asynchronously. */
    //clientsCron();


    /* Clear the paused clients flag if needed. */
    //clientsArePaused(); /* Don't check return value, just use the side effect.*/


    server.cronloops++;
    return 1000/server.hz;
}

void closeListeningSockets(void) {
    int j;

    for (j = 0; j < server.ipfd_count; j++) {
        close(server.ipfd[j]);
    }
}

int prepareForShutdown(void) {

    serverLog(LL_WARNING,"User requested shutdown...");

    /* Remove the pid file if possible and needed. */
    /* TODO re-add this!
    if (server.daemonize || server.pidfile) {
        serverLog(LL_NOTICE,"Removing the pid file.");
        unlink(server.pidfile);
    }
    */

    /* Close the listening sockets. Apparently this allows faster restarts. */
    closeListeningSockets();
    serverLog(LL_WARNING,"Server is now ready to exit, bye bye...");
    return C_OK;
}

void setupSignalHandlers(void) {
    // TODO!!!
}

/* Return the UNIX time in microseconds */
long long ustime(void) {
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec)*1000000;
    ust += tv.tv_usec;
    return ust;
}

void updateCachedTime(int update_daylight_info) {
    server.ustime = ustime();
    server.mstime = server.ustime / 1000;
    server.unixtime = server.mstime / 1000;

    /* To get information about daylight saving time, we need to call
     * localtime_r and cache the result. */
    if (update_daylight_info) {
        struct tm tm;
        time_t ut = server.unixtime;
        localtime_r(&ut,&tm);
        server.daylight_active = tm.tm_isdst;
    }
}

/* This function gets called every time Redis is entering the
 * main loop of the event driven library, that is, before to sleep
 * for ready file descriptors. */
void beforeSleep(struct aeEventLoop *eventLoop) {
    UNUSED(eventLoop);

    /* Try to process pending commands for clients that were just unblocked. */
    //if (listLength(server.unblocked_clients))
    //    processUnblockedClients();

    //TODO does this really belong here?
    handleClientsWithPendingWrites();

    /* Close clients that need to be closed asynchronous */
    freeClientsInAsyncFreeQueue();
}

int listenToPort(int port, int *fds, int *count) {
    int j;

    /* Force binding of 0.0.0.0 if no bind address is specified, always
     * entering the loop if j == 0. */
    if (server.bindaddr_count == 0) {
        server.bindaddr[0] = NULL;
    }

    for (j = 0; j < server.bindaddr_count || j == 0; j++) {
        if (server.bindaddr[j] == NULL) {
            int unsupported = 0;
            /* Bind * for both IPv6 and IPv4, we enter here only if
             * server.bindaddr_count == 0. */
            fds[*count] = anetTcp6Server(server.neterr,port,NULL, server.tcp_backlog);
            if (fds[*count] != ANET_ERR) {
                anetNonBlock(NULL,fds[*count]);
                (*count)++;
            } else if (errno == EAFNOSUPPORT) {
                unsupported++;
                serverLog(LL_WARNING,"Not listening to IPv6: unsupported");
            }

            if (count) {
                break;
            }
            if (unsupported) {
                /* Bind the IPv4 address as well. */
                fds[*count] = anetTcpServer(server.neterr,port,NULL, server.tcp_backlog);
                if (fds[*count] != ANET_ERR) {
                    anetNonBlock(NULL,fds[*count]);
                    (*count)++;
                } else if (errno == EAFNOSUPPORT) {
                    unsupported++;
                    serverLog(LL_WARNING,"Not listening to IPv4: unsupported");
                }
            }
            /* Exit the loop if we were able to bind * on IPv4 and IPv6,
             * otherwise fds[*count] will be ANET_ERR and we'll print an
             * error and return to the caller with an error. */
            if (*count + unsupported == 2) break;
        } else if (strchr(server.bindaddr[j],':')) {
            /* Bind IPv6 address. */
            fds[*count] = anetTcp6Server(server.neterr,port,server.bindaddr[j],
                server.tcp_backlog);
        } else {
            /* Bind IPv4 address. */
            fds[*count] = anetTcpServer(server.neterr,port,server.bindaddr[j],
                server.tcp_backlog);
        }
        if (fds[*count] == ANET_ERR) {
            serverLog(LL_WARNING,
                "Could not create server TCP listening socket %s:%d: %s",
                server.bindaddr[j] ? server.bindaddr[j] : "*",
                port, server.neterr);
                if (errno == ENOPROTOOPT     || errno == EPROTONOSUPPORT ||
                    errno == ESOCKTNOSUPPORT || errno == EPFNOSUPPORT ||
                    errno == EAFNOSUPPORT    || errno == EADDRNOTAVAIL)
                    continue;
            return C_ERR;
        }
        anetNonBlock(NULL,fds[*count]);
        (*count)++;
    }
    return C_OK;
}

ServerCommand *lookupCommand(sds name) {
    if(strcasecmp(name, send_command_table_command.name) == 0) {
        return &send_command_table_command;
    }

    ServerCommand* command = server_command_table;
    while(command->func || command->legacy_func) {
        if(strcasecmp(name, command->name) == 0) {
            return command;
        }
        command++;
    }
    return NULL;
}

ServerCommand *lookupCommandByCString(char *s) {
    ServerCommand* cmd;
    sds name = sdsnew(s);

    cmd = lookupCommand(name);
    sdsfree(name);
    return cmd;
}

/* If this function gets called we already read a whole
 * command, arguments are in the client argv/argc fields.
 * processCommand() execute the command or prepare the
 * server for a bulk read from the client.
 *
 * If C_OK is returned the client is still alive and valid and
 * other operations can be performed by the caller. Otherwise
 * if C_ERR is returned the client was destroyed (i.e. after QUIT). */
int processCommand(client *c) {
    if (!strcasecmp(c->argv[0], "quit")) {
        addReplyString(c, "+OK");
        c->flags |= CLIENT_CLOSE_AFTER_REPLY;
        return C_ERR;
    }

    /* Now lookup the command and check ASAP about trivial error conditions
     * such as wrong arity, bad command name and so forth. */
    c->cmd = c->lastcmd = lookupCommand(c->argv[0]);
    if (!c->cmd) {
        sds args = sdsempty();
        int i;
        for (i=1; i < c->argc && sdslen(args) < 128; i++) {
            args = sdscatprintf(args, "`%.*s`, ", 128-(int)sdslen(args), (char*)c->argv[i]);
        }
        addReplyErrorFormat(c, "unknown command `%s`, with args beginning with: %s",
            (char*)c->argv[0], args);
        sdsfree(args);
        return C_OK;
    } else if ((c->cmd->nargs > 0 && c->cmd->nargs != c->argc) || (c->argc < -c->cmd->nargs)) {
        addReplyErrorFormat(c,"wrong number of arguments for '%s' command. Expects: %i, Got: %i",
            c->cmd->name, c->cmd->nargs-1, c->argc-1);
        return C_OK;
    }

    /* Exec the command */
    call(c, CMD_CALL_FULL);
    return C_OK;
}

void call(client *c, int flags) {
    long long start, duration;
    int i;
    ServerCommand *real_cmd = c->cmd;

    /* Call the command. */
    updateCachedTime(0);
    start = server.ustime;

    if(server.server_call) {
        server.server_call(c);
    }
    else if(real_cmd->func) {
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
    duration = ustime()-start;

    if(flags & CMD_CALL_LOG) {
        serverLog(LL_VERBOSE, "Command %s executed", c->cmd->name);
    }



    if (flags & CMD_CALL_STATS) {
        /* use the real command that was executed (cmd and lastamc) may be
         * different, in case of MULTI-EXEC or re-written commands such as
         * EXPIRE, GEOADD, etc. */
        real_cmd->microseconds += duration;
        real_cmd->calls++;
    }
    server.stat_numcommands++;
}

void initServer(void) {
    int j;

    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    setupSignalHandlers();

    // TODO uncomment this!
    //if (server.syslog_enabled) {
    //    openlog(server.syslog_ident, LOG_PID | LOG_NDELAY | LOG_NOWAIT,
    //        server.syslog_facility);
    //}

    server.hz = server.config_hz;
    server.pid = getpid();
    server.current_client = NULL;
    server.clients = listCreate();
    //server.clients_index = raxNew();
    server.clients_to_close = listCreate();
    server.clients_pending_write = listCreate();
    server.clients_pending_read = listCreate();
    server.stat_net_input_bytes = 0;
    server.stat_net_output_bytes = 0;
    server.stat_rejected_conn = 0;
    server.stat_numconnections = 0;
    server.stat_total_writes_processed = 0;
    server.mstime = 0;
    server.ustime =0;
    server.server_call = NULL;


    //createSharedObjects();
    //adjustOpenFilesLimit();
    server.el = aeCreateEventLoop(server.maxclients+CONFIG_FDSET_INCR);
    if (server.el == NULL) {
        serverLog(LL_WARNING,
            "Failed creating the event loop. Error message: '%s'",
            strerror(errno));
        exit(1);
    }

    /* Open the TCP listening socket for the user commands. */
    if (server.port != 0 && listenToPort(server.port, server.ipfd, &server.ipfd_count) == C_ERR) {
        exit(1);
    }


    /* Abort if there are no listening sockets at all. */
    // - Eric M Probably could delete the below thing, I'm not as flexible as real redis
    if (server.ipfd_count == 0) {
        serverLog(LL_WARNING, "Configured to not listen anywhere, exiting.");
        exit(1);
    }

    /* initialize internal state. */
    server.cronloops = 0;
    //server.dirty = 0;

    /* Create the timer callback, this is our way to process many background
     * operations incrementally, like clients timeout, eviction of unaccessed
     * expired keys and so forth. */
    if (aeCreateTimeEvent(server.el, 1, serverCron, NULL, NULL) == AE_ERR) {
        serverPanic("Can't create event loop timers.");
        exit(1);
    }

    /* Create an event handler for accepting new connections in TCP and Unix
     * domain sockets. */
    for (j = 0; j < server.ipfd_count; j++) {
        if (aeCreateFileEvent(server.el, server.ipfd[j], AE_READABLE,
            acceptTcpHandler,NULL) == AE_ERR)
            {
                serverPanic("Unrecoverable error creating server.ipfd file event.");
            }
    }
}

void serverSetCustomCall(ServerCallFunc call_func) {
    server.server_call = call_func;
}

void serverClearCustomCall() {
    server.server_call = NULL;
}
