#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>

#include "server.h"
#include "ae.h"

#define ANET_OK 0
#define ANET_ERR -1
#define ANET_ERR_LEN 1024

// Stole from redis/server.c

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
    // TODO
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

static int anetTcpServer(char *err, int port, char *bindaddr, int af, int backlog) {
    int s = -1, rv;
    char _port[6];  /* strlen("65535") */
    struct addrinfo hints, *servinfo, *p;

    snprintf(_port,6,"%d",port);
    memset(&hints,0,sizeof(hints));
    hints.ai_family = af;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    /* No effect if bindaddr != NULL */

    if ((rv = getaddrinfo(bindaddr,_port,&hints,&servinfo)) != 0) {
        anetSetError(err, "%s", gai_strerror(rv));
        return ANET_ERR;
    }
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((s = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) == -1)
            continue;

        if (anetSetReuseAddr(err,s) == ANET_ERR) goto error;
        if (anetListen(err,s,p->ai_addr,p->ai_addrlen,backlog) == ANET_ERR) s = ANET_ERR;
        goto end;
    }
    if (p == NULL) {
        anetSetError(err, "unable to bind socket, errno: %d", errno);
        goto error;
    }

error:
    if (s != -1) close(s);
    s = ANET_ERR;
end:
    freeaddrinfo(servinfo);
    return s;
}

void initServerConfig(void) {
    int j;

   // server.runid[CONFIG_RUN_ID_SIZE] = '\0';
   // server.timezone = getTimeZone(); /* Initialized by tzset(). */
    server.hz = server.config_hz = CONFIG_DEFAULT_HZ;
    server.dynamic_hz = CONFIG_DEFAULT_DYNAMIC_HZ;
    server.arch_bits = (sizeof(long) == 8) ? 64 : 32;
    server.port = CONFIG_DEFAULT_SERVER_PORT;
    server.bindaddr_count = 0;
    server.maxclients = CONFIG_DEFAULT_MAX_CLIENTS;
    //server.unixsocket = NULL;
    //server.unixsocketperm = CONFIG_DEFAULT_UNIX_SOCKET_PERM;
    //server.maxidletime = CONFIG_DEFAULT_CLIENT_TIMEOUT;
    //server.tcpkeepalive = CONFIG_DEFAULT_TCP_KEEPALIVE;
    //server.active_expire_enabled = 1;
    //server.proto_max_bulk_len = CONFIG_DEFAULT_PROTO_MAX_BULK_LEN;
    //server.client_max_querybuf_len = PROTO_MAX_QUERYBUF_LEN;
    //server.daemonize = CONFIG_DEFAULT_DAEMONIZE;
    //server.maxclients = CONFIG_DEFAULT_MAX_CLIENTS;
    //server.blocked_clients = 0;
    //memset(server.blocked_clients_by_type,0, sizeof(server.blocked_clients_by_type));
    //server.shutdown_asap = 0;


    /* Client output buffer limits */
   // for (j = 0; j < CLIENT_TYPE_OBUF_COUNT; j++) {
   //     server.client_obuf_limits[j] = clientBufferLimitsDefaults[j];
   // }

}

int serverCron(struct aeEventLoop* eventLoop, long long id, void *clientData) {

    /* TODO!!!!! */
    server.cronloops++;
    return 0;
}

void setupSignalHandlers(void) {
    // TODO!!!
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
    //server.call_depth = 0;
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
