/* Copyright (c) 2020, Haakan T. Johansson */
/* All rights reserved. */

/* Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the authors nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "fnet_client.h"

#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/socket.h>

/*************************************************************************/

struct fnet_ctrl_client
{
  /* Socket file descriptor. */
  int    _fd;

  /* Sequence number of the UDP connection. */
  int    _sequence_number;

  /* TODO: use a few different, depending on packet size. */
  int    _udp_timeout_usec;

  /* Most recent error description. */
  const char *_last_error;

  /* IPv4 address used for connection. */
  size_t             _addrlen;
  struct sockaddr_in _serv_addr;
  struct sockaddr_in _recv_addr; /* Address where we got last packet from. */

  /* File handle for debug messages. */
  FILE *_debug_fid;

  /* Statistics. */
  fnet_ctrl_client_stats _stats;

  /* Buffers for send and receive UDP packets. */
  char _buf_send[sizeof (fakernet_reg_access) +
         FAKERNET_REG_ACCESS_MAX_ITEMS *
         sizeof (fakernet_reg_acc_item)];
  char _buf_recv[sizeof (fakernet_reg_access) +
         FAKERNET_REG_ACCESS_MAX_ITEMS *
         sizeof (fakernet_reg_acc_item)];
};

/*************************************************************************/

#define MAX_ATTEMPTS 10

/*************************************************************************/

/*
  fnet_ctrl_connect
  +- fnet_ctrl_create_client
  |  -> client
  |  -> NULL => NULL
  +- for ( ; attempts ; )
  |  +- fnet_establish_connection
  |     -> 1 => client
  | -> 
  => NULL

  fnet_establish_connection
  +- for ( ; attempts ; )
     +- fnet_send_recv_packet
     |  +- fnet_check_reg_reset_reply
     +- fnet_send_recv_packet
        +- fnet_check_reg_reset_reply

  fnet_terminate_connection
  +- for ( ; attempts ; )
     +- fnet_send_recv_packet
        +- fnet_check_reg_reset_reply
        -> 1 => ok

  fnet_send_recv_packet
  +- for ( ; attempts ; )
     +- sendto
     +- recvfrom
     +- PTR check_reply ()
        -> 0  (malformed)
        -> <0 (refusal)
  -> 1 (success)
  -> 0 (failure)

 */




/*************************************************************************/

#define FNET_CTRL_DEBUG(...) fnet_ctrl_debug_printf(client, __VA_ARGS__)

#define FNET_CTRL_ERROR(error_str) do {       \
    FNET_CTRL_DEBUG("Error: " error_str);     \
    client->_last_error = error_str;          \
  } while (0)

void fnet_ctrl_debug_printf(struct fnet_ctrl_client *client,
                const char *fmt, ...)
  __attribute__ ((__format__ (__printf__, 2, 3)));

void fnet_ctrl_debug_printf(struct fnet_ctrl_client *client,
                const char *fmt, ...)
{
  va_list ap;

  if (client->_debug_fid)
    {
      struct timeval now;

      gettimeofday(&now, NULL);
      
      fprintf(client->_debug_fid,"%02d.%06d: ",
          (int) (now.tv_sec % 100), (int) now.tv_usec);

      va_start(ap, fmt);
      vfprintf(client->_debug_fid, fmt, ap);
      va_end(ap);
      fprintf(client->_debug_fid, "\n");
      fflush(client->_debug_fid);
    }
}

/*************************************************************************/

typedef int(*fnet_check_reply_func)(struct fnet_ctrl_client *client,
                    const void *buf_send, size_t len_send,
                    const void *buf_recv, size_t len_recv);

/*************************************************************************/

int fnet_check_reg_reset_reply(struct fnet_ctrl_client *client,
                   const void *buf_send, size_t len_send,
                   const void *buf_recv, size_t len_recv)
{
  const fakernet_reg_access *send = (const fakernet_reg_access *) buf_send;
  const fakernet_reg_access *recv = (const fakernet_reg_access *) buf_recv;

  (void) client;
  (void) len_send;

  if (len_recv != sizeof (fakernet_reg_access))
    {
      FNET_CTRL_DEBUG("Wrong arm/reset response length.");
      return 0;
    }
  /*
  printf ("%04x %04x -> [%04x %04x] %04x %04x\n",
      ntohs(send->sequence_request),
      ntohs(send->sequence_response),
      ntohs(recv->status_udp_channels),
      ntohs(recv->status_tcp),
      ntohs(recv->sequence_request),
      ntohs(recv->sequence_response));
  */
  if (ntohs(recv->sequence_request))
    {
      FNET_CTRL_DEBUG("Nonzero request in arm/reset response.");
      return 0;
    }
  if ((ntohs(send->sequence_request) & 0xe000) == 0x8000)
    {
      if ((ntohs(send->sequence_request) & 0x0f00) ==
      (~ntohs(recv->sequence_response) & 0x0f00))
    {
      FNET_CTRL_DEBUG("Arming not possible, active.");
      return -1;
    }
      if ((ntohs(send->sequence_request) & 0x0f00) !=
      (ntohs(recv->sequence_response) & 0x0f00))
    {
      FNET_CTRL_DEBUG("Bad key response for arming.");
      return 0;
    }
      
      return 1;
    }
  else if ((ntohs(send->sequence_request) & 0xe000) == 0x4000)
    {
      if (ntohs(recv->sequence_response) ==
      (0x4400 | ~(0x0003 & send->sequence_request)))
    {
      FNET_CTRL_DEBUG("Not armed, or wrong sequence number.");
      return 0;
    }
      
      if (ntohs(send->sequence_request) !=
      ntohs(recv->sequence_response))
    return 0;

      return 1;
    }
  else if ((ntohs(send->sequence_request) & 0xe000) == 0x2000)
    {
      if (ntohs(send->sequence_request) !=
      ntohs(recv->sequence_response))
    return 0;

      return 1;
    }
  return 0;
}

/*************************************************************************/

int fnet_check_reg_access_reply(struct fnet_ctrl_client *client,
                const void *buf_send, size_t len_send,
                const void *buf_recv, size_t len_recv)
{
  const fakernet_reg_access *send = (const fakernet_reg_access *) buf_send;
  const fakernet_reg_access *recv = (const fakernet_reg_access *) buf_recv;
  size_t items, i;
  int fail = 0;
  
  FNET_CTRL_DEBUG("got: sl: %08zx rl: %08zx "
          "sseqreq: %08x rseqreq: %08x rseqrsp: %08x",
          len_send, len_recv,
          ntohs(send->sequence_request),
          ntohs(recv->sequence_request),
          ntohs(recv->sequence_response));
  
  if (len_send != len_recv)
    return 0;

  if (ntohs(recv->sequence_request))
    {
      FNET_CTRL_DEBUG("Nonzero request in response.");
      return 0;
    }
  if (ntohs(recv->sequence_response) != ntohs(send->sequence_request))
    {
      if (ntohs(recv->sequence_response) ==
      ((ntohs(send->sequence_request) - 1) & 0x3fff))
    {
      client->_stats.prev_reg_sequence++;
      FNET_CTRL_DEBUG("Wrong (previous) sequence number.");
      /* This happens if we timed out last time, send another
       * request, but then received the response of the first one.
       * Pileup.
       */
    }
      else
    {
      FNET_CTRL_DEBUG("Wrong sequence number.");
    }
      return 0;
    }

  items =
    (len_send - sizeof (fakernet_reg_access)) / sizeof (fakernet_reg_acc_item);

  for (i = 0; i < items; i++)
    {
      const fakernet_reg_acc_item *sitem = &send->items[i];
      const fakernet_reg_acc_item *ritem = &recv->items[i];
      uint32_t saddr = ntohl(sitem->addr);
      uint32_t sflags = saddr & 0xf0000000;
      uint32_t raddr = ntohl(ritem->addr);
      uint32_t rflags = raddr & 0xf0000000;
      saddr &= 0x0fffffff;
      raddr &= 0x0fffffff;

      FNET_CTRL_DEBUG("%2zd: %08x %08x %08x %08x",
              i, saddr, raddr, sflags, rflags);

      if (saddr != raddr)
    {
      FNET_CTRL_DEBUG("Response address mismatch request address.");
      return 0;
    }
      if (sflags == FAKERNET_REG_ACCESS_ADDR_WRITE &&
      rflags != FAKERNET_REG_ACCESS_ADDR_WRITTEN)
    {
      FNET_CTRL_DEBUG("Write failed (addr %08x, got 0x%08x).",
                saddr & 0x0fffffff,
                raddr);
      fail |= 2;
    }
      if (sflags == FAKERNET_REG_ACCESS_ADDR_READ &&
      rflags != FAKERNET_REG_ACCESS_ADDR_READ_RET)
    {
      FNET_CTRL_DEBUG("Read failed (addr 0x%08x, got 0x%08x).",
                saddr & 0x0fffffff,
                raddr);
      fail |= 4;
    }
    }
  return 1 | fail;
}

/*************************************************************************/

int fnet_send_recv_packet(struct fnet_ctrl_client *client,
              int num_items,
              int resend, int max_attempts,
              fnet_check_reply_func check_reply)
{
  ssize_t n;
  int attempt;
  int ret;
  socklen_t recv_addr_len;

  void *buf_send = client->_buf_send;
  void *buf_recv = client->_buf_recv;
  size_t len_send;
  size_t len_recv;
  size_t maxlen_recv;

  struct timeval timeout;
  struct timeval t_send;
  struct timeval t_recv;

  time_t diff_s;
  int elapsed_usec;

  {
    fakernet_reg_access *regacc = (fakernet_reg_access *) client->_buf_send;
  
    /* These items are for return values only, and shall always be zero
     * for requests.
     */
    regacc->status_udp_channels = htons(0);
    regacc->status_tcp = htons(0);
    regacc->sequence_response = htons(0);
  }
  
  len_send =
    sizeof (fakernet_reg_access) +
    num_items * sizeof (fakernet_reg_acc_item);

  maxlen_recv = sizeof (client->_buf_recv);

  /*
  fprintf (stderr, "send:");
  for (size_t i = 0; i < len_send / sizeof (uint32_t); i++)
    {
      fprintf (stderr, " %08x",ntohl(((uint32_t *) buf_send)[i]));
    }
  fprintf (stderr, "\n");
  */

  /* Setup the timeout. */
  timeout.tv_sec = 0;
  timeout.tv_usec = 10000;

  ret = setsockopt(client->_fd, SOL_SOCKET, SO_RCVTIMEO,
           &timeout, sizeof(timeout));
  /* FNET_CTRL_DEBUG("ret: %d\n", ret); */

  /* Since we only read one packet per send, first make sure there are
   * no old packets in the queue.
   */
  for ( ; ; )
    {
      struct sockaddr *recv_addr = (struct sockaddr *) &client->_recv_addr;
      fd_set read_fds;
      
      FD_ZERO(&read_fds);
      FD_SET(client->_fd, &read_fds);

      /* Poll. */
      timeout.tv_sec = 0;
      timeout.tv_usec = 0;

      ret = select(client->_fd + 1,
           &read_fds, NULL, NULL, &timeout);

      if (ret == 0)
    break;

      if (ret < 0)
    {
      if (errno == EINTR)
        continue;
      perror("select");
      return -1;
    }

      FNET_CTRL_DEBUG("poll reported packet in queue before send");

      /* There is some packet waiting.  Try to read it. */

      n = recvfrom(client->_fd, buf_recv, maxlen_recv, 0,
           recv_addr,
           &recv_addr_len);

      if (n == -1)
    {
      if (errno == EINTR ||
          errno == EAGAIN ||
          errno == EWOULDBLOCK)
        continue;

      perror("recvfrom");
      break;
    }
  }  

  /* Measure the time from first send until a successful response. */
  gettimeofday(&t_send, NULL);

  for (attempt = 0; attempt < max_attempts; attempt++)
    {
      struct sockaddr *serv_addr = (struct sockaddr *) &client->_serv_addr;
      struct sockaddr *recv_addr = (struct sockaddr *) &client->_recv_addr;
      
      if (attempt == 0 || resend)
    {      
      n = sendto(client->_fd, buf_send, len_send, 0,
             serv_addr,
             (socklen_t) client->_addrlen);

      if (n == -1)
        {
          if (errno == EINTR)
        continue;
          perror("sendto");
          return -1;
        }
    }

      recv_addr_len = (socklen_t) client->_addrlen;

      n = recvfrom(client->_fd, buf_recv, maxlen_recv, 0,
           recv_addr,
           &recv_addr_len);

      if (n == -1)
    {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN ||
          errno == EWOULDBLOCK)
        {
          /* TODO: make this message come if it has not happened for a while.
           */
          if (attempt > max_attempts / 2)
        {
          FNET_CTRL_DEBUG("timeout");
        }
          client->_stats.recv_timeout++;
          continue;
        }
      
      perror("recvfrom");
    }

      /* Check that the source address and port is correct. */

      if (recv_addr_len != client->_addrlen)
    {
      FNET_CTRL_DEBUG("wrong/different address size of reply "
                "(%x!=%d)",
                (int) recv_addr_len,
                (int) client->_addrlen);
      continue;
    }
      else if (recv_addr->sa_family != AF_INET)
    {
      FNET_CTRL_DEBUG("wrong/unknown address family of reply (%d)",
                recv_addr->sa_family);
      continue;
    }
      else /* AF_INET */
    {
      const struct sockaddr_in *addr_in =
        (const struct sockaddr_in *) serv_addr;
      struct sockaddr_in *addr_recv_in =
        (struct sockaddr_in *) recv_addr;

      if (addr_recv_in->sin_addr.s_addr != addr_in->sin_addr.s_addr)
        {
          FNET_CTRL_DEBUG("got spurious reply from "
                  "wrong address (%08x != %08x)",
                  ntohl(addr_recv_in->sin_addr.s_addr),
                  ntohl(addr_in->sin_addr.s_addr));
          continue;
        }

      if (addr_recv_in->sin_port != addr_in->sin_port)
        {
          FNET_CTRL_DEBUG("got spurious reply from "
                  "wrong port (%d != %d)",
                  ntohs(addr_recv_in->sin_port),
                  ntohs(addr_in->sin_port));
          continue;
        }
    }

      /*
      fprintf (stderr, "recv:");
      for (size_t i = 0; i < n / sizeof (uint32_t); i++)
    {
      fprintf (stderr, " %08x",ntohl(((uint32_t *) buf_recv)[i]));
    }
      fprintf (stderr, "\n");
      */
      
      ret = check_reply(client, buf_send, len_send, buf_recv, n);

      if (ret == 0)
    {
      FNET_CTRL_DEBUG("malformed reply");
      client->_stats.malformed_packet++;
      continue;
    }
      if (ret < 0)
    {
      FNET_CTRL_DEBUG("refusal reply");
      return -1;
    }

      len_recv = n; /* never used... */ (void) len_recv;

      gettimeofday(&t_recv, NULL);

      diff_s = t_recv.tv_sec - t_send.tv_sec;

      if (diff_s < 0 || diff_s > 100)
    elapsed_usec = 100 * 1000000;
      else
    elapsed_usec =
      1000000 * (int) diff_s + (t_recv.tv_usec - t_send.tv_usec);

      if (client->_udp_timeout_usec > elapsed_usec * 4)
    client->_udp_timeout_usec -= (client->_udp_timeout_usec / 100 + 1);
      else if (client->_udp_timeout_usec < elapsed_usec * 2)
    client->_udp_timeout_usec += (client->_udp_timeout_usec / 10 + 1);

      if (client->_udp_timeout_usec < 20)
    client->_udp_timeout_usec = 20;
      else if (client->_udp_timeout_usec > 100000)
    client->_udp_timeout_usec = 100000;

      /*
      printf ("elapsed: %d  timeout: %d\n",
          elapsed_usec, client->_udp_timeout_usec);
      */
      
      return 1;
    }
  return 0;
}

/*************************************************************************/

int fnet_establish_connection(struct fnet_ctrl_client *client,
                  int allow_connected)
{
  int ret;

  int attempts;
  int key = (int) time(NULL);

  for (attempts = 0; attempts < MAX_ATTEMPTS; attempts++)
    {  
      fakernet_reg_access *regacc = (fakernet_reg_access *) client->_buf_send;
      fakernet_reg_access *regres = (fakernet_reg_access *) client->_buf_recv;

      key = (key + 1) & 0xf;

      regacc->sequence_request  =
    htons(FAKERNET_SEQ_REQ_RESET_ARM |
          (key << FAKERNET_SEQ_REQ_ARM_USER_CODE_SHIFT));

      FNET_CTRL_DEBUG("Request arm: 0x%04x",
              ntohs(regacc->sequence_request));

      ret =
    fnet_send_recv_packet(client, 0 /* num_items */,
                  0 /* resend */, MAX_ATTEMPTS,
                  fnet_check_reg_reset_reply);

      if (ret < 0)
    {
      FNET_CTRL_DEBUG("Refused to arm reset (is active).");
      return 0;
    }
      
      if (ret == 0)
    {
      FNET_CTRL_DEBUG("Failed to arm reg sequence reset.");
      continue;
    }

      if (!allow_connected &&
      (ntohs(regres->sequence_response) & FAKERNET_SEQ_REQ_CONNECTED))
    {
      FNET_CTRL_DEBUG("Do not use arm - channel is connected.");
      return 0;
    }

      int armno =
    ntohs(regres->sequence_response) & FAKERNET_SEQ_SEQUENCE_MASK;

      FNET_CTRL_DEBUG("Armed: 0x%04x", ntohs(regres->sequence_response));

      regacc->sequence_request  = htons(FAKERNET_SEQ_REQ_RESET | armno);
      regacc->sequence_response = htons(0);

      FNET_CTRL_DEBUG("Request reset: 0x%04x",
            ntohs(regacc->sequence_request));
      
      ret =
    fnet_send_recv_packet(client, 0 /* num_items */,
                  1 /*resend */, MAX_ATTEMPTS,
                  fnet_check_reg_reset_reply);

      if (ret <= 0)
    {
      FNET_CTRL_DEBUG("Failed to do reg sequence reset.");
      continue;
    }

      client->_sequence_number =
    (ntohs(regres->sequence_response) & FAKERNET_SEQ_SEQUENCE_MASK) ^
    FAKERNET_SEQ_SEQUENCE_MASK_1ST_XOR;
      client->_sequence_number =
    (client->_sequence_number + 1) & FAKERNET_SEQ_SEQUENCE_MASK;

      FNET_CTRL_DEBUG("Reset done: 0x%04x (start_packet: 0x%4x)",
              ntohs(regres->sequence_response),
              client->_sequence_number);

      return 1;
    }
  return 0;
}

/*************************************************************************/

int fnet_terminate_connection(struct fnet_ctrl_client *client)
{
  int ret;

  int attempts;

  for (attempts = 0; attempts < MAX_ATTEMPTS; attempts++)
    {  
      fakernet_reg_access *regacc = (fakernet_reg_access *) client->_buf_send;
      fakernet_reg_access *regres = (fakernet_reg_access *) client->_buf_recv;
      (void) regres;

      regacc->sequence_request  =
    htons(FAKERNET_SEQ_REQ_DISCONNECT);

      FNET_CTRL_DEBUG("Disconnect: 0x%04x",
              ntohs(regacc->sequence_request));

      ret =
    fnet_send_recv_packet(client, 0 /* num_items */,
                  0 /* resend */, MAX_ATTEMPTS,
                  fnet_check_reg_reset_reply);

      if (ret <= 0)
    {
      FNET_CTRL_DEBUG("Failed to disconnect.");
      continue;
    }

      return 1;
    }

  return 0;
}

/*************************************************************************/

struct fnet_ctrl_client *fnet_ctrl_alloc_client(void)
{
  struct fnet_ctrl_client *client;

  client = (struct fnet_ctrl_client *)
    malloc (sizeof (struct fnet_ctrl_client));

  if (!client)
    {
      errno = ENOMEM;
      return NULL;
    }

  client->_fd = -1;
  client->_sequence_number = -1;

  client->_udp_timeout_usec = 0;

  client->_last_error = NULL;

  client->_debug_fid = NULL;

  memset(&client->_stats, 0, sizeof (client->_stats));

  return client;
}

/*************************************************************************/

int fnet_ctrl_close(struct fnet_ctrl_client *client)
{
  /* No matter what, we will go through with the close.
   * But we can still report errors.
   */
  
  fnet_terminate_connection(client);

  close(client->_fd);

  free(client);

  return 0;
}

/*************************************************************************/

/* Public functions:
 */

struct fnet_ctrl_client *fnet_ctrl_connect(const char *server,
                       int reliable,
                       const char **error_ptr,
                       FILE *debug)
{
  struct fnet_ctrl_client *client;

  if (error_ptr)
    *error_ptr = NULL;

  if (!(client = fnet_ctrl_alloc_client()))
    {
      if (error_ptr)
          *error_ptr = "Memory allocation failure.";
      return NULL; /* errno already set */
    }

  client->_debug_fid = debug;

  {
    struct hostent *h;
    char *hostname, *colon;
    int port_parse = FAKERNET_DEFAULT_CTRL_PORT_BASE;
    unsigned short port;
  
    /* If there is a colon in the hostname, interpret what follows
     * as a port number, overriding the default port.
     */
    hostname = strdup(server);
    colon = strchr(hostname,':');

    if (colon)
      {
    *colon = 0; /* cut the hostname */
    port_parse = atoi(colon+1);
      }

    port = (unsigned short) port_parse;

    /* Get server IP address. */
    h = gethostbyname(hostname);
    free(hostname);

    if(h == NULL)
      {
    FNET_CTRL_ERROR("Hostname not found.");
    /* EHOSTDOWN is not really correct, but the best I could find. */
    errno = EHOSTDOWN;
    goto errno_return_NULL;
      }

    /*
        INFO("Server '%s' known... (IP : %s).", h->h_name,
        inet_ntoa(*(struct in_addr *)h->h_addr_list[0]));
    */
    
    /* Create the socket. */
    
    client->_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (client->_fd == -1)
      {
    /* errno already set */
    FNET_CTRL_ERROR("Failed to create socket.");
    goto errno_return_NULL;
      }

    /* Build the server's Internet address. */
    memset(&client->_serv_addr, 0, sizeof (client->_serv_addr));
    client->_serv_addr.sin_family = (sa_family_t) h->h_addrtype;
    memcpy((char *) &client->_serv_addr.sin_addr.s_addr,
       h->h_addr_list[0], (size_t) h->h_length);
    client->_serv_addr.sin_port = htons(port);
    client->_addrlen = sizeof (client->_serv_addr);

    if (!reliable)
      {
    int portno = port;

    FNET_CTRL_DEBUG("Trying idempotent UDP channel = port %d",
            portno);

    client->_serv_addr.sin_port = htons(portno);

    /* Check that an access using 0 items gives a reponse.
     * Otherwise other end is simply missing, in which case we
     * should not report success.
     */

    if (fnet_ctrl_send_recv_regacc(client, 0))
      return client;

    FNET_CTRL_ERROR("No reponse on idempotent UDP access channel.");
    errno = ENODEV;
    goto errno_return_NULL;
      }

    /* Try to establish connection on a free channel. */
    for (int attempts = 0; attempts < 2; attempts++)
    {
      for (int allow_connected = 0; allow_connected < 2; allow_connected++)
    {
      /* TODO: we should get the number of reliable
       * control channels from the system.
       */
      for (int porti = 0; porti < 2; porti++)
        {
          int portno = port + 1 /* idempotent */ + porti;
      
          FNET_CTRL_DEBUG("Trying reliable UDP channel %d = port %d",
                  porti, portno);
      
          client->_serv_addr.sin_port = htons(portno);

          if (fnet_establish_connection(client,
                        allow_connected))
        {
          /* Sucess! */
          return client;
        }
        }
    }
      sleep(1);
    }
    FNET_CTRL_ERROR("No free UDP access channel.");
    errno = EBUSY;
    goto errno_return_NULL;
  }
  

 errno_return_NULL:
  /* Free the allocated client buffer.
   * The data buffer also, if allocated.
   */
  {
    int errsv = errno;
    if (error_ptr)
      *error_ptr = client->_last_error;
    /* We do not care about close errors. */
    if (client->_fd != -1)
      close(client->_fd);
    /* Cannot fail, could possibly change errno. */
    free(client);
    errno = errsv;
  }
  return NULL;
  
}

/*************************************************************************/

void fnet_ctrl_get_send_recv_bufs(struct fnet_ctrl_client *client,
                  fakernet_reg_acc_item **send,
                  fakernet_reg_acc_item **recv)
{
  *send = ((fakernet_reg_access *) client->_buf_send)->items;
  if (recv)
    *recv = ((fakernet_reg_access *) client->_buf_recv)->items;
}

/*************************************************************************/

int fnet_ctrl_send_recv_regacc(struct fnet_ctrl_client *client, int num_items) {
    fakernet_reg_access *regacc = (fakernet_reg_access *) client->_buf_send;
    int ret;

    regacc->sequence_request =
        htons((uint16_t) ((client->_sequence_number) &
                    (FAKERNET_SEQ_SEQUENCE_MASK |
                     FAKERNET_SEQ_REQ_ARM_USER_CODE_MASK)));

    ret =
        fnet_send_recv_packet(client, num_items,
                1 /* resend */, MAX_ATTEMPTS,
                fnet_check_reg_access_reply);

    if (ret >= 1)
        client->_sequence_number++;

    client->_stats.reg_requests++;

    return ret;
}

/*************************************************************************/

void fnet_ctrl_get_last_udp_status(struct fnet_ctrl_client *client,
                   uint32_t *status_udp_channels,
                   uint32_t *status_tcp)
{
  fakernet_reg_access *regacc = (fakernet_reg_access *) client->_buf_recv;
  
  *status_udp_channels = ntohs(regacc->status_udp_channels);
  *status_tcp          = ntohs(regacc->status_tcp);
}

/*************************************************************************/

int fnet_ctrl_reset_tcp(struct fnet_ctrl_client *client)
{
  fakernet_reg_acc_item *send;
  int ret;
  int num_send;

  fnet_ctrl_get_send_recv_bufs(client, &send, NULL);

  /* Reset TCP. */

  send[0].addr =
    htonl(FAKERNET_REG_ACCESS_ADDR_WRITE |
      FAKERNET_REG_ACCESS_ADDR_RESET_TCP);
  send[0].data = htonl(0);

  num_send = 1;

  ret = fnet_ctrl_send_recv_regacc(client, num_send);

  if (ret <= 0)
    return ret;

  FNET_CTRL_DEBUG("TCP reset done.");

  return 0;
}

/*************************************************************************/

#define MAX_ATTEMPT_TCP_CONNECT  3

int fnet_ctrl_open_tcp(struct fnet_ctrl_client *client)
{
  struct sockaddr_in addr_in;
  int tcp_sockfd;
  int ret;
  int i;

  for (i = 0; ; i++)
    {
      ret = fnet_ctrl_reset_tcp(client);

      if (ret < 0)
    return ret;

      fnet_ctrl_get_sockaddr_in(client, &addr_in);

      tcp_sockfd = socket(AF_INET, SOCK_STREAM, 0);
  
      if (tcp_sockfd < 0)
    {
      /* errno already set */
      FNET_CTRL_ERROR("Failed to create socket for TCP.");
      return -1;
    }

      ret = connect(tcp_sockfd,
            (struct sockaddr *) &addr_in,
            sizeof (addr_in));

      /* printf ("%d %d\n", tcp_sockfd, ret); */

      if (ret == 0)
    return tcp_sockfd;

      /* We have to try again.  Close the socket. */

      {
    int errsv = errno;
    close (tcp_sockfd);
    errno = errsv;
      }

      if (i < MAX_ATTEMPT_TCP_CONNECT)
    continue;

      /* errno already set. */
      FNET_CTRL_ERROR("Failed to connect socket for TCP.");
      return -1;
    }
}

/*************************************************************************/

int fnet_ctrl_get_sockaddr_in(struct fnet_ctrl_client *client,
                  struct sockaddr_in *addr_in)
{
  if (!client)
    return 1;
  
  /* Copy the address data. */
  
  memcpy(addr_in, &client->_serv_addr, sizeof (struct sockaddr_in));

  /* Set the port to the TCP port (as this is what it would be used for. */
  
  addr_in->sin_port = htons(1 /* TCP PORT */);

  return 0;
}

/*************************************************************************/

const char *fnet_ctrl_last_error(struct fnet_ctrl_client *client)
{
  return client->_last_error;
}

/*************************************************************************/

int fnet_ctrl_get_udp_timeout_usec(struct fnet_ctrl_client *client)
{
  return client->_udp_timeout_usec;
}

/*************************************************************************/

fnet_ctrl_client_stats *fnet_ctrl_get_stats(struct fnet_ctrl_client *client)
{
  return &client->_stats;
}

/*************************************************************************/
