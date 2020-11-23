#ifndef FAKE_LWIP_H
#define FAKE_LWIP_H
#include <stdio.h>
#include <stdint.h>

#define ERR_OK 0
struct tcp_pcb;
typedef int err_t;
typedef uint16_t u16_t;
typedef err_t (*tcp_cbk)(void* arg, struct tcp_pcb*, u16_t);
void initialize_lwip();
uint32_t check_netif();
int tcp_write(struct tcp_pcb* client, void* data, u16_t len, int opts);
int tcp_output(struct tcp_pcb* client);
int tcp_sent(struct tcp_pcb* client, tcp_cbk cb);
int tcp_arg(struct tcp_pcb*, void* data);
int tcp_sndbuf(struct tcp_pcb*);
void clean_up_fds();
#endif
