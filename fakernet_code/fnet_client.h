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

#ifndef __FNET_CLIENT_H__
#define __FNET_CLIENT_H__

#include "fakernet.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <netinet/in.h>

/*************************************************************************/

struct fnet_ctrl_client;

/*************************************************************************/

/* Connect to an Fakernet UDP control channel.
 *
 * @server          Either a HOSTNAME or a HOSTNAME:PORT.
 * @reliable        If 1, then establish a dedicated access channel
 *                  that verifies that accesses are performed once
 *                  using sequence numbers in the access request
 *                  packets.  Since this requires the Fakernet
 *                  system to keep state, there is only a limited
 *                  number of such access channels.
 *                  If 0, the first port is used, which only is safe
 *                  for idempotent accesses, i.e. such that may be
 *                  executed several times without harm.  This would
 *                  typically be read-only actions, e.g. status queries.
 *                  The first port does not verify increasing sequence
 *                  numbers, and thus can any number of clients.
 * @error_ptr       Pointer to a string pointer to receive a more
 *                  descriptive error in case of connection failure,
 *                  or NULL.
 *
 * There is a limit to reliable access channels: after not being used
 * for a while (about 1 s), the access channel can be reset by another
 * client that needs a reliable access channel.  It is therefore not
 * useful to hold them open for a long time.
 *
 * Return value:
 *
 * Pointer to a context structure (use when calling other functions).
 * NULL on failure, in which case errno describes the error.
 *
 * In addition to various system (socket etc) errors, errno:
 *
 * EPROTO           Protocol error, version mismatch?  (not used)
 * ENOMEM           Failure to allocate memory.
 * EBUSY            All UDP access channels are busy.
 *
 * EHOSTDOWN        Hostname not found (not really correct).
 */

struct fnet_ctrl_client *fnet_ctrl_connect(const char *server,
					   int reliable,
					   const char **error_ptr,
					   FILE *debug);

/*************************************************************************/

/* Close a client connection, and free the context structure.
 *
 * @client          Connection context structure.
 *
 * Return value:
 *
 *  0  success.
 * -1  failure.  See errno.
 *
 * In addition to various system (socket close) errors, errno:
 *
 * EFAULT           @client is NULL, or not properly set up.
 */

int fnet_ctrl_close(struct fnet_ctrl_client *client);

/*************************************************************************/

/* Get pointers to memory areas to prepare register access requests,
 * and the response.
 *
 * The areas are used by fnet_send_recv_regacc().
 *
 * @send            Area for request.
 * @recv            Area for response.
 *
 */


void fnet_ctrl_get_send_recv_bufs(struct fnet_ctrl_client *client,
				  fakernet_reg_acc_item **send,
				  fakernet_reg_acc_item **recv);

/*************************************************************************/

/* Perform one UDP control request.
 *
 * The request shall be prepared in the memory areas provided by
 * fnet_get_send_recv_bufs().
 *
 * The request shall consist of n 2x32-bit entries, where the first of
 * each pair is an address (and direction) mark, and the second holds
 * the write value (or has space for the read return value).
 *
 * Return value:
 *
 *  0          success (one event written (buffered)).
 * -1          failure.  See errno.
 * -2          failure, by performer (not communication).
 *
 * In addition to various system (socket write) errors, errno:
 *
 * EINVAL           @size is wrong.
 * EBADMSG          Data offset outside structure.
 *                  Malformed message.  Bug?
 * EPROTO           Unexpected message.  Bug?
 * ETIMEOUT         Access failed despite repeated attempts.
 * EFAULT           @client is NULL.
 *
 * 'errno' should be considered a second hand choice for reporting the
 * nature of an error to the user.  Rather use fnet_ctrl_last_error().
 */


int fnet_ctrl_send_recv_regacc(struct fnet_ctrl_client *client,
			       int num_items);

/*************************************************************************/

/* Perform a UDP control to reset the TCP state.
 *
 * Note: if a TCP session is active, it will no longer respond (likely
 * without reporting the closed connection, since Fakernet does not).
 *
 * Return value:
 *
 *  0          success.
 * -1          failure.  See errno.
 * -2          failure, by performer (not communication).
 *
 * In addition to various system (socket write) errors, errno:
 *
 * ETIMEOUT         Access failed despite repeated attempts.
 */

int fnet_ctrl_reset_tcp(struct fnet_ctrl_client *client);

/*************************************************************************/

/* Open a TCP connection.  This is just a convenience function,
 * that calls socket(2) and connect(2).
 *
 * The UDP control request to reset the TCP state is also called.
 *
 * It is likely a good idea to call fnet_ctrl_close() after the
 * connection has been established.
 *
 * Return value:
 *
 *  0          success.
 * -1          failure.  See errno.
 * -2          failure, by performer (not communication).
 *
 * In addition to various system (socket write) errors, errno:
 *
 * ETIMEOUT         Access failed despite repeated attempts.
 */

int fnet_ctrl_open_tcp(struct fnet_ctrl_client *client);

/*************************************************************************/

/* Report common information gathered at the last UDP access.
 * Mostly useful for debugging.
 */

void fnet_ctrl_get_last_udp_status(struct fnet_ctrl_client *client,
				   uint32_t *status_udp_channels,
				   uint32_t *status_tcp);

/*************************************************************************/

/* Get the sockaddr for the host address used.
 *
 * This is a convenience function to avoid the host lookup when
 * establishing TCP connections, since fnet_ctrl_connect() already has
 * done the host name lookup.
 *
 * The port number will be set to the TCP port used by Fakernet (1).
 *
 * It is still the responsibility of the using code to call
 * create the socket and connect it, using socket() and connect().
 *
 * @addr_in         Socket address.  IPv4 structure, since only IPv4 is
 *                  supported by Fakernet.
 *
 * In addition to various system (socket write) errors, errno:
 *
 * EFAULT           @client is NULL.
 */

int fnet_ctrl_get_sockaddr_in(struct fnet_ctrl_client *client,
			      struct sockaddr_in *addr_in);

/*************************************************************************/

/* Return a pointer to a (static) string with a more descriptive error
 * message of the most recent failure.
 */

const char *fnet_ctrl_last_error(struct fnet_ctrl_client *client);

/*************************************************************************/
  
/* Return the file descriptor associated with the client.
 */

int fnet_ctrl_get_fd(struct fnet_ctrl_client *client);

/*************************************************************************/

/* These are for internal use / development purposes only! */

extern FILE *_fnet_debug_fid;

void fnet_local_printf(int debug, const char *fmt, ...)
  __attribute__ ((__format__ (__printf__, 2, 3)));

typedef struct fnet_ctrl_client_stats_t
{
  int recv_timeout;
  int malformed_packet;

  int reg_requests;
  int prev_reg_sequence;

} fnet_ctrl_client_stats;

int fnet_ctrl_get_udp_timeout_usec(struct fnet_ctrl_client *client);

fnet_ctrl_client_stats *fnet_ctrl_get_stats(struct fnet_ctrl_client *client);

/*************************************************************************/

#endif/*__FNET_CLIENT_H__*/
