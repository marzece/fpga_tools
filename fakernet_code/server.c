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
#include <stdarg.h> // TODO Remove this once _serverPanic & _serverAssert are removed
#include <stdio.h> // TODO Remove this once _serverPanic & _serverAssert are removed

#include "server_common.h"
#include "server.h"
#include "ae.h"

#define ANET_OK 0
#define ANET_ERR -1
#define ANET_ERR_LEN 1024

/* Output buffer limits presets. */
clientBufferLimitsConfig clientBufferLimitsDefaults = {0, 0, 0}; /* normal */


struct Server server;
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

void serverLog(int level, const char* fmt, ...) {
    (void) level;
    (void) fmt;
    // TODO
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


static void anetSetError(char *err, const char *fmt, ...) {
    va_list ap;

    if (!err) return;
    va_start(ap, fmt);
    vsnprintf(err, ANET_ERR_LEN, fmt, ap);
    va_end(ap);
}

static int anetSetReuseAddr(char *err, int fd) {
    int yes = 1;
    /* Make sure connection-intensive things like the redis benchmark
     * will be able to close/open sockets a zillion of times */
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        anetSetError(err, "setsockopt SO_REUSEADDR: %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

static int anetListen(char *err, int s, struct sockaddr *sa, socklen_t len, int backlog) {
    if (bind(s,sa,len) == -1) {
        anetSetError(err, "bind: %s", strerror(errno));
        close(s);
        return ANET_ERR;
    }

    if (listen(s, backlog) == -1) {
        anetSetError(err, "listen: %s", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    return ANET_OK;
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
    //server.unixsocket = NULL;
    //server.unixsocketperm = CONFIG_DEFAULT_UNIX_SOCKET_PERM;
    //server.maxidletime = CONFIG_DEFAULT_CLIENT_TIMEOUT;
    //server.tcpkeepalive = CONFIG_DEFAULT_TCP_KEEPALIVE;
    //server.active_expire_enabled = 1;
    server.proto_max_bulk_len = CONFIG_DEFAULT_PROTO_MAX_BULK_LEN;
    //server.client_max_querybuf_len = PROTO_MAX_QUERYBUF_LEN;
    //server.daemonize = CONFIG_DEFAULT_DAEMONIZE;
    //server.maxclients = CONFIG_DEFAULT_MAX_CLIENTS;
    //server.blocked_clients = 0;
    //memset(server.blocked_clients_by_type,0, sizeof(server.blocked_clients_by_type));
    //server.shutdown_asap = 0;

    /* Client output buffer limits */
    // -- Eric M. client_obuf_limits used to be an array with a set of limits for each "type" of client.
    // Should I ever try and re-add pubsub connections I'll need to make this an array again
    server.client_obuf_limits = clientBufferLimitsDefaults;
}

int serverCron(struct aeEventLoop* eventLoop, long long id, void *clientData) {
    (void) eventLoop;
    (void) id;
    (void) clientData;
    /* TODO!!!!! */
    server.cronloops++;
    return 0;
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
     * localtime_r and cache the result. However calling localtime_r in this
     * context is safe since we will never fork() while here, in the main
     * thread. The logging function will call a thread safe version of
     * localtime that has no locks. */
    if (update_daylight_info) {
        struct tm tm;
        time_t ut = server.unixtime;
        localtime_r(&ut,&tm);
        server.daylight_active = tm.tm_isdst;
    }
}

int listenToPort(int port, int *fds, int *count) {
    int j;

    /* Force binding of 0.0.0.0 if no bind address is specified, always
     * entering the loop if j == 0. */
    if (server.bindaddr_count == 0) server.bindaddr[0] = NULL;
    for (j = 0; j < server.bindaddr_count || j == 0; j++) {
        if (server.bindaddr[j] == NULL) {
            int unsupported = 0;
            /* Bind * for both IPv6 and IPv4, we enter here only if
             * server.bindaddr_count == 0. */
            fds[*count] = anetTcp6Server(server.neterr,port,NULL,
                server.tcp_backlog);
            if (fds[*count] != ANET_ERR) {
                anetNonBlock(NULL,fds[*count]);
                (*count)++;
            } else if (errno == EAFNOSUPPORT) {
                unsupported++;
                serverLog(LL_WARNING,"Not listening to IPv6: unsupported");
            }

            if (*count == 1 || unsupported) {
                /* Bind the IPv4 address as well. */
                fds[*count] = anetTcpServer(server.neterr,port,NULL,
                    server.tcp_backlog);
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
    // !TODO!!!
    //return dictFetchValue(server.commands, name);
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
    /* The QUIT command is handled separately. Normal command procs will
     * go through checking for replication and QUIT will cause trouble
     * when FORCE_REPLICATION is enabled and would be implemented in
     * a regular command proc. */
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
        for (i=1; i < c->argc && sdslen(args) < 128; i++)
            args = sdscatprintf(args, "`%.*s`, ", 128-(int)sdslen(args), (char*)c->argv[i]);
        addReplyErrorFormat(c, "unknown command `%s`, with args beginning with: %s",
            (char*)c->argv[0], args);
        sdsfree(args);
        return C_OK;
    } else if ((c->cmd->nargs > 0 && c->cmd->nargs != c->argc) ||
               (c->argc < -c->cmd->nargs)) {
        addReplyErrorFormat(c,"wrong number of arguments for '%s' command",
            c->cmd->name);
        return C_OK;
    }

    /* Exec the command */
    call(c, CMD_CALL_FULL);
    return C_OK;
}

void call(client *c, int flags) {
    long long start, duration;
    ServerCommand *real_cmd = c->cmd;

    /* Call the command. */
    updateCachedTime(0);
    start = server.ustime;
    c->cmd->func(c, c->argc, c->argv);
    duration = ustime()-start;

    if(flags & CMD_CALL_LOG) {
        // TODO!
        //serverLog();
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
    //server.clients = listCreate();
    //server.clients_index = raxNew();
    //server.clients_to_close = listCreate();
    //server.slaves = listCreate();
    //server.monitors = listCreate();
    //server.clients_pending_write = listCreate();
    //server.clients_pending_read = listCreate();
    //server.slaveseldb = -1; /* Force to emit the first SELECT command. */
    //server.unblocked_clients = listCreate();
    //server.ready_keys = listCreate();
    //server.clients_waiting_acks = listCreate();
    //server.get_ack_from_slaves = 0;
    //server.clients_paused = 0;
    //server.system_memory_size = zmalloc_get_memory_size();
    server.stat_net_input_bytes = 0;
    server.stat_net_output_bytes = 0;
    server.stat_rejected_conn = 0;
    server.stat_numconnections = 0;


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
    if (server.port != 0 &&
        listenToPort(server.port,server.ipfd,&server.ipfd_count) == C_ERR)
        exit(1);


    /* Abort if there are no listening sockets at all. */
    // - Eric M Probably could delete the below thing, I'm not as flexible as real redis
    if (server.ipfd_count == 0) {
        serverLog(LL_WARNING, "Configured to not listen anywhere, exiting.");
        exit(1);
    }

    /* Create the Redis databases, and initialize other internal state. */
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
