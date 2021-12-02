#ifndef _FNET_SERVER_H
#define _FNET_SERVER_H
#include <sys/socket.h>
#include "sds.h"
#include "server_common.h"
#include "connection.h"
#include "adlist.h"
#include "anet.h"
#include "ae.h"

/*TODO list
 *  - Add log file
 *  - Add verbosity
 *  - UNIX domain sockets
 */

// Log levels
#define LL_DEBUG 0
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
#define PROTO_IOBUF_LEN (1024*16)
#define NET_IP_STR_LEN 46 /* INET6_ADDRSTRLEN is 46, but we need to be sure */

#define CONFIG_DEFAULT_PROTO_MAX_BULK_LEN (512ll*1024*1024) /* Bulk request max size */

/* Protocol and I/O related defines */
#define PROTO_REQ_INLINE 1
#define PROTO_REQ_MULTIBULK 2
#define PROTO_MAX_QUERYBUF_LEN  (1024*1024*1024) /* 1GB max query buffer. */
#define PROTO_IOBUF_LEN         (1024*16)  /* Generic I/O buffer size */
#define PROTO_REPLY_CHUNK_BYTES (16*1024) /* 16k output buffer */
#define PROTO_INLINE_MAX_SIZE   (1024*64) /* Max size of inline reads */
#define PROTO_MBULK_BIG_ARG     (1024*32)
#define LONG_STR_SIZE      21          /* Bytes needed for long -> str + '\0' */
#define REDIS_AUTOSYNC_BYTES (1024*1024*32) /* fdatasync every 32MB */

#define LIMIT_PENDING_QUERYBUF (4*1024*1024) /* 4mb */


/* Client flags */
#define CLIENT_SLAVE (1<<0)   /* This client is a repliaca */
#define CLIENT_MASTER (1<<1)  /* This client is a master */
#define CLIENT_MONITOR (1<<2) /* This client is a slave monitor, see MONITOR */
#define CLIENT_MULTI (1<<3)   /* This client is in a MULTI context */
#define CLIENT_BLOCKED (1<<4) /* The client is waiting in a blocking operation */
#define CLIENT_DIRTY_CAS (1<<5) /* Watched keys modified. EXEC will fail. */
#define CLIENT_CLOSE_AFTER_REPLY (1<<6) /* Close after writing entire reply. */
#define CLIENT_UNBLOCKED (1<<7) /* This client was unblocked and is stored in
                                  server.unblocked_clients */
#define CLIENT_ASKING (1<<9)     /* Client issued the ASKING command */
#define CLIENT_CLOSE_ASAP (1<<10)/* Close this client ASAP */
#define CLIENT_UNIX_SOCKET (1<<11) /* Client connected via Unix domain socket */
#define CLIENT_DIRTY_EXEC (1<<12)  /* EXEC will fail for errors while queueing */
#define CLIENT_MASTER_FORCE_REPLY (1<<13)  /* Queue replies even if is master */
#define CLIENT_FORCE_AOF (1<<14)   /* Force AOF propagation of current cmd. */
#define CLIENT_PRE_PSYNC (1<<16)   /* Instance don't understand PSYNC. */
#define CLIENT_READONLY (1<<17)    /* Cluster client is in read-only state. */
#define CLIENT_PUBSUB (1<<18)      /* Client is in Pub/Sub mode. */
#define CLIENT_PENDING_WRITE (1<<21) /* Client has output to send but a write
                                        handler is yet not installed. */
#define CLIENT_REPLY_OFF (1<<22)   /* Don't send replies to client. */
#define CLIENT_REPLY_SKIP_NEXT (1<<23)  /* Set CLIENT_REPLY_SKIP for next cmd */
#define CLIENT_REPLY_SKIP (1<<24)  /* Don't send just this reply. */
#define CLIENT_PROTECTED (1<<28) /* Client should not be freed for now. */
#define CLIENT_PENDING_READ (1<<29) /* The client has pending reads and was put
                                       in the list of clients we can read
                                       from. */
#define CLIENT_PENDING_COMMAND (1<<30) /* Used in threaded I/O to signal after
                                          we return single threaded that the
                                          client has already pending commands
                                          to be executed. */
#define CLIENT_TRACKING (1<<31) /* Client enabled keys tracking in order to
                                   perform client side caching. */
#define CLIENT_TRACKING_BROKEN_REDIR (1ULL<<32) /* Target client is invalid. */

/* Command call flags, see call() function */
#define CMD_CALL_NONE 0
#define CMD_CALL_LOG (1<<0)
#define CMD_CALL_STATS (1<<1)
#define CMD_CALL_FULL (CMD_CALL_LOG | CMD_CALL_STATS)


// serverAssertWithInfo Copied from Tony
#define serverAssertWithInfo(_c,_o,_e) ((_e)?(void)0 : (fprintf(stderr, "blah\n"),exit(1)))
//#define serverAssertWithInfo(_c,_o,_e) ((_e)?(void)0 : (_serverAssertWithInfo(_c,_o,#_e,__FILE__,__LINE__),exit(1)))
#define serverAssert(_e) ((_e)?(void)0 : (_serverAssert(#_e,__FILE__,__LINE__),exit(1)))
#define serverPanic(...) _serverPanic(__FILE__, __LINE__, __VA_ARGS__), exit(1)


/* This structure is used in order to represent the output buffer of a client,
 * which is actually a linked list of blocks like that, that is: client->reply. */
typedef struct clientReplyBlock {
    size_t size, used;
    char buf[];
} clientReplyBlock;

typedef struct clientBufferLimitsConfig {
    unsigned long long hard_limit_bytes;
    unsigned long long soft_limit_bytes;
    time_t soft_limit_seconds;
} clientBufferLimitsConfig;

extern clientBufferLimitsConfig clientBufferLimitsDefaults;


typedef struct client {
    uint64_t id;            /* Client incremental unique ID. */
    sds name;               /* Name of client for logging/debugging purposes */
    connection *conn;
    int resp;               /* RESP protocol version. Can be 2 or 3. */
    sds querybuf;           /* Buffer we use to accumulate client queries. */
    size_t qb_pos;          /* The position we have read in querybuf. */
    sds pending_querybuf;   /* If this client is flagged as master, this buffer
                               represents the yet not applied portion of the
                               replication stream that we are receiving from
                               the master. */
    size_t querybuf_peak;   /* Recent (100ms or more) peak of querybuf size. */
    int argc;               /* Num of arguments of current command. */
    sds* argv;            /* Arguments of current command. */
    ServerCommand *cmd, *lastcmd;  /* Last command executed. */
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
    // TODO Re-add block functionality
    //int btype;              /* Type of blocking op if CLIENT_BLOCKED. */
    //blockingState bpop;     /* blocking state */
    listNode *client_list_node; /* list node in client list */

    /* Response buffer */
    int bufpos;
    char buf[PROTO_REPLY_CHUNK_BYTES];
} client;

struct  Server {
    pid_t pid;
    int ipfd[CONFIG_BINDADDR_MAX]; /* TCP socket file descriptors */
    int ipfd_count;             /* Used slots in ipfd[] */
    unsigned int maxclients;
    int port;
    int tcp_backlog;            /* TCP listen() backlog */
    int config_hz;
    int hz;
    int dynamic_hz;
    int tcpkeepalive;
    int arch_bits;
    int verbosity;                  /* Loglevel in redis.conf */
    char *bindaddr[CONFIG_BINDADDR_MAX]; /* Addresses we should bind to */
    int bindaddr_count;         /* Number of addresses in server.bindaddr[] */
    int daemonize;
    int cronloops;
    uint64_t next_client_id; // TODO make sure this gets initted to 1
    list* clients;
    list* clients_to_close;
    list *clients_pending_write; /* There is to write or install handler. */
    list *clients_pending_read;  /* Client has pending read socket buffers. */
    unsigned int blocked_clients;   /* # of clients executing a blocking cmd.*/
    client* current_client;
    char neterr[ANET_ERR_LEN];   /* Error buffer for anet.c */
    long long proto_max_bulk_len;   /* Protocol bulk length maximum size. */
    time_t unixtime;    /* Unix time sampled every cron cycle. */
    time_t timezone;            /* Cached timezone. As set by tzset(). */
    int daylight_active;        /* Currently in daylight saving time. */
    long long mstime;            /* 'unixtime' in milliseconds. */
    long long ustime;            /* 'unixtime' in microseconds. */
    int shutdown_asap;          /* SHUTDOWN needed ASAP */


    aeEventLoop *el;
    long long stat_net_input_bytes; /* Bytes read from network. */
    long long stat_net_output_bytes; /* Bytes written to network. */
    long long stat_rejected_conn;   /* Clients rejected because of maxclients */
    long long stat_numconnections;  /* Number of connections received */
    long long stat_numcommands;     /* Number of processed commands */
    long long stat_total_writes_processed; /* Total number of writes processed */
    size_t client_max_querybuf_len; /* Limit for client query buffer length */
    clientBufferLimitsConfig client_obuf_limits;
    // logfile;
};

extern struct Server server;
extern ServerCommand* server_command_table;
void send_command_table(client* c, int argc, sds* argv);

void daemonize(void);
void initServerConfig(void);
void initServer(void);
long long ustime(void); /* Get linux time microseconds */

void _serverAssert(const char *estr, const char *file, int line);
void _serverPanic(const char* file, int line, const char* msg, ...);

void acceptTcpHandler(struct aeEventLoop *el, int fd, void *privdata, int mask);

int clientHasPendingReplies(client *c);
void clientInstallWriteHandler(client *c);
//static void anetSetError(char *err, const char *fmt, ...);
//static int anetSetReuseAddr(char *err, int fd);
//static int anetListen(char *err, int s, struct sockaddr *sa, socklen_t len, int backlog);
//static int anetTcpServer(char *err, int port, char *bindaddr, int af, int backlog);
int listenToPort(int port, int *fds, int *count);
void processInputBuffer(client *c);
int processMultibulkBuffer(client *c);
int processCommand(client *c);
int processCommandAndResetClient(client *c);
void call(client *c, int flags);

sds catClientInfoString(sds s, client *client);
void freeClient(client *c);
void freeClientAsync(client *c);
int freeClientsInAsyncFreeQueue(void);
void resetClient(client *c);
void asyncCloseClientOnOutputBufferLimitReached(client *c);
void addReplyProto(client *c, const char *s, size_t len);
void addReplyString(client *c, const char *s);
void addReplyErrorLength(client *c, const char *s, size_t len);
void addReplyError(client *c, const char *err) ;
void addReplyErrorFormat(client *c, const char *fmt, ...);
void addReplyStatus(client *c, const char *status);
void addReplyStatusFormat(client *c, const char *fmt, ...);
void addReplyLongLongWithPrefix(client *c, long long ll, char prefix);
void addReplyLongLong(client *c, long long ll);
ServerCommand* lookupCommand(sds name);
ServerCommand* lookupCommandByCString(char *s) ;
void beforeSleep(struct aeEventLoop *eventLoop);
int handleClientsWithPendingWrites(void);
void updateCachedTime(int update_daylight_info);
int prepareForShutdown(void);
void closeListeningSockets(void);


#ifdef __GNUC__
void serverLog(int level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
void serverLog(int level, const char *fmt, ...);
#endif

#endif
