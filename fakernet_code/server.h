#ifndef _FNET_SERVER_H
#define _FNET_SERVER_H
#include <sys/socket.h>

/*TODO list
 *  - Add log file
 *  - Add verbosity
 *  - UNIX domain sockets
 *  - Put back server stats
 *
 *
 */
// Log levels
#define LL_DEBUGk 0
#define LL_VERBOSE 1
#define LL_NOTICE 2
#define LL_WARNING 3
#define CONFIG_DEFAULT_VERBOSITY LL_NOTICE

#define C_OK 0
#define C_ERR -1

#define CONFIG_DEFAULT_HZ        10             /* Time interrupt calls/sec. */
#define CONFIG_DEFAULT_DYNAMIC_HZ 1             /* Adapt hz to # of clients.*/
#define CONFIG_DEFAULT_SERVER_PORT 4002         /* Default listening port */
#define CONFIG_DEFAULT_MAX_CLIENTS 1000
#define CONFIG_MIN_RESERVED_FDS 32
#define CONFIG_FDSET_INCR (CONFIG_MIN_RESERVED_FDS+96)
#define CONFIG_BINDADDR_MAX 16
#define PROTO_REPLY_CHUNK_BYTES (16*1024)

#define serverPanic(...) _serverPanic(__FILE__, __LINE__, __VA_ARGS__), exit(1)

struct  Server {
    pid_t pid;
    void* current_client; // TODO look up the correct type of this !
    int ipfd[CONFIG_BINDADDR_MAX];
    int ipfd_count;
    unsigned int maxclients;
    int port;
    int config_hz;
    int hz;
    int dynamic_hz;
    int arch_bits;
    int bindaddr_count;
    int daemonize;
    int cronloops;

    struct aeEventLoop *el;
    // logfile;
};

typedef struct client {
    uint64_t id;            /* Client incremental unique ID. */
    connection *conn;
    int resp;               /* RESP protocol version. Can be 2 or 3. */
    //robj *name;             /* As set by CLIENT SETNAME. */
    sds querybuf;           /* Buffer we use to accumulate client queries. */
    size_t qb_pos;          /* The position we have read in querybuf. */
    sds pending_querybuf;   /* If this client is flagged as master, this buffer
                               represents the yet not applied portion of the
                               replication stream that we are receiving from
                               the master. */
    size_t querybuf_peak;   /* Recent (100ms or more) peak of querybuf size. */
    int argc;               /* Num of arguments of current command. */
    char **argv;            /* Arguments of current command. */
    struct redisCommand *cmd, *lastcmd;  /* Last command executed. */
    int reqtype;            /* Request protocol type: PROTO_REQ_* */
    int multibulklen;       /* Number of multi bulk arguments left to read. */
    long bulklen;           /* Length of bulk argument in multi bulk request. */
    list *reply;            /* List of reply objects to send to the client. */
    unsigned long long reply_bytes; /* Tot bytes of objects in reply list. */
    size_t sentlen;         /* Amount of bytes already sent in the current
                               buffer or object being sent. */
    time_t ctime;           /* Client creation time. */
    time_t lastinteraction; /* Time of the last interaction, used for timeout */
    time_t obuf_soft_limit_reached_time;
    uint64_t flags;         /* Client flags: CLIENT_* macros. */
    int btype;              /* Type of blocking op if CLIENT_BLOCKED. */
    blockingState bpop;     /* blocking state */
    sds peerid;             /* Cached peer ID. */
    listNode *client_list_node; /* list node in client list */

    /* Response buffer */
    int bufpos;
    char buf[PROTO_REPLY_CHUNK_BYTES];
} client;

extern struct Server server;
void daemonize(void);
void initServerConfig(void);
void initServer(void);

void _serverPanic(const char* file, int line, const char* msg, ...);

void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask);

static void anetSetError(char *err, const char *fmt, ...);
static int anetSetReuseAddr(char *err, int fd);
static int anetListen(char *err, int s, struct sockaddr *sa, socklen_t len, int backlog);
static int anetTcpServer(char *err, int port, char *bindaddr, int af, int backlog);
int listenToPort(int port, int *fds, int *count);
#endif
