#ifndef _FNET_SERVER_H
#define _FNET_SERVER_H
#include <sys/socket.h>
static void anetSetError(char *err, const char *fmt, ...);
static int anetSetReuseAddr(char *err, int fd);
static int anetListen(char *err, int s, struct sockaddr *sa, socklen_t len, int backlog);
static int anetTcpServer(char *err, int port, char *bindaddr, int af, int backlog);
#endif
