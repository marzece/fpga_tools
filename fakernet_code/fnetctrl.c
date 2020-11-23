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

#include "fakernet.h"
#include "fnet_client.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <inttypes.h>
#include <math.h>

#define NUM_VER_REGISTERS     4
#define NUM_DEBUG_REGISTERS  32
#define NUM_DEBUG_COUNTERS   64

#define FLAG_WORDS_DIV_32   0x01
#define FLAG_IS_SLOW_TICKS  0x02
#define FLAG_IS_DIFFABLE    0x04

typedef struct index_name_t
{
  int          index;
  int          flags;
  const char  *name;
} index_name;

void fnet_ctrl_stat(struct fnet_ctrl_client *client)
{
  uint32_t prev_cnt[NUM_DEBUG_COUNTERS];
  uint32_t prev_val[NUM_DEBUG_REGISTERS];
  struct timeval t_prev, t_now, t_after;
  double dt_prev = 0.0;
  int packets;
  int ret;

  memset(prev_cnt, 0, sizeof (prev_cnt));
  memset(prev_val, 0, sizeof (prev_val));
  timerclear(&t_prev);

  for (packets = 0; ; packets++)
    {
      fakernet_reg_acc_item *send;
      fakernet_reg_acc_item *recv;
      int num_send;

      fnet_ctrl_get_send_recv_bufs(client, &send, &recv);
    
      {
    int k;
    int kk = 0;

    for (k = 0; k < NUM_VER_REGISTERS; k++, kk++)
      {
        send[kk].addr =
          htonl(FAKERNET_REG_ACCESS_ADDR_READ |
            (FAKERNET_REG_ACCESS_ADDR_VERSION + k));
        send[kk].data = htonl(0);
      }

    for (k = 0; k < NUM_DEBUG_REGISTERS; k++, kk++)
      {
        send[kk].addr =
          htonl(FAKERNET_REG_ACCESS_ADDR_READ |
            (FAKERNET_REG_ACCESS_ADDR_INT_STAT + k));
        send[kk].data = htonl(0);
      }

    for (k = 0; k < NUM_DEBUG_COUNTERS; k++, kk++)
      {
        send[kk].addr =
          htonl(FAKERNET_REG_ACCESS_ADDR_READ |
            (FAKERNET_REG_ACCESS_ADDR_INT_COUNT + k));
        send[kk].data = htonl(0);
      }

    num_send = kk;
      }

      t_prev = t_now;
      gettimeofday(&t_now, NULL);

      ret = fnet_ctrl_send_recv_regacc(client, num_send);
        
      if (ret <= 0)
    {
      fprintf (stderr, "Failed: %s\n",
           fnet_ctrl_last_error(client));
      continue;
    }

      gettimeofday(&t_after, NULL);

      {
    int k;
    int kk = 0;
    uint32_t sum_counts = 0;
    uint32_t slow_tick_diff = 0;

    uint32_t c_ver[NUM_VER_REGISTERS];
    uint32_t c_cnt[NUM_DEBUG_COUNTERS];
    uint32_t c_val[NUM_DEBUG_REGISTERS];

    double elapsed, dt;

    elapsed = (t_now.tv_sec - t_prev.tv_sec) +
      0.000001 * (t_now.tv_usec - t_prev.tv_usec);
    dt = (t_after.tv_sec - t_now.tv_sec) +
      0.000001 * (t_after.tv_usec - t_now.tv_usec);

    double slow_tick_time = 0;

    for (k = 0; k < NUM_VER_REGISTERS; k++, kk++)
      {
        c_ver[k] = ntohl(recv[kk].data);
        
        if (k % 4 == 0)
          printf("%03x:", 0x10 + k);
        printf(" %08x", c_ver[k]);
        if (k % 4 == 3)
          printf("\n");
      }
    for (k = 0; k < NUM_DEBUG_REGISTERS; k++, kk++)
      {
        c_val[k] = ntohl(recv[kk].data);
        
        if (k % 8 == 0)
          printf("%03x:", 0x20 + k);
        printf(" %08x", c_val[k]);
        if (k % 8 == 7)
          printf("\n");
      }
    for (k = 0; k < NUM_DEBUG_COUNTERS; k++, kk++)
      {
        uint32_t diff;
        
        c_cnt[k] = ntohl(recv[kk].data);
        diff = (c_cnt[k] - prev_cnt[k]);

        sum_counts += diff;
        if (k == 26)
          slow_tick_diff = diff;

        if (k % 8 == 0)
          printf("%03x:", 0x200 + k);
        printf(" %08x", c_cnt[k]);
        if (k % 8 == 7)
          printf("\n");
      }
    printf("\n");

    {
      uint32_t status_udp_channels;
      uint32_t status_tcp;
      const char *tcp_conn_str = "?";
      time_t ct;
      char ct_date[64];

      fnet_ctrl_get_last_udp_status(client,
                    &status_udp_channels, &status_tcp);

      switch (status_tcp & FAKERNET_STATUS2_TCP_CONN_MASK)
        {
        case FAKERNET_STATUS2_TCP_IDLE:
          tcp_conn_str = "idle";
          break;
        case FAKERNET_STATUS2_TCP_GOT_SYN:
          tcp_conn_str = "syn";
          break;
        case FAKERNET_STATUS2_TCP_CONNECTED:
          tcp_conn_str = "connected";
          break;
        }
        
      ct = (time_t) c_ver[0];
      strftime(ct_date,sizeof(ct_date),"%Y-%m-%d %H:%M:%S UTC",gmtime(&ct));

      printf("UDP: connected %02x, active %02x               "
         "Compiled: %s\n",
         status_udp_channels & 0xff,
         (status_udp_channels >> 8) & 0xff,
         ct_date);
      printf("TCP: %s%s%s%s%s\n",
         tcp_conn_str,
         status_tcp &
         FAKERNET_STATUS2_TCP_COMMIT_OVERFLOW ?
         " COMMIT-OVERFLOW" : "",
         status_tcp &
         FAKERNET_STATUS2_TCP_WRITE_OVERFLOW ?
         " WRITE-OVERFLOW" : "",
         status_tcp & FAKERNET_STATUS2_TCP_RAM_BITFLIP ?
         " TCP-RAM-BITFLIP" : "",
         status_tcp & FAKERNET_STATUS2_ANY_RAM_BITFLIP ?
         " ANY-RAM-BITFLIP" : "");
      printf("\n");
    }

    if (timerisset(&t_prev))
      slow_tick_time = elapsed / slow_tick_diff;
    printf("Total counts: %10u (%7.3f MHz +/- %5.3f)\n",
           sum_counts,
           sum_counts * 4 / elapsed * 1e-6,
           sum_counts * 4 / elapsed * 1e-6 *
           (dt + dt_prev) / elapsed);
    printf("Slow ticks:   %10u (%d) (%.2f us)\n",
           slow_tick_diff,
           sum_counts / slow_tick_diff,
           slow_tick_time * 1.e6);
    printf("\n");

    dt_prev = dt;
      
    {
      const index_name lcl_reg_names[] = {
        {  0, 0, "" },
        {  1, FLAG_IS_DIFFABLE,
           /**/  "tcp_stat.base_seqno" },
        {  2, 0, "tcp_stat.max_sent" },
        {  3, 0, "tcp_stat.filled" },
        {  4, 0, "tcp_stat.unsent" },
        {  5, 0, "tcp_stat.unfilled" },
        {  6, 0, "tcp_stat.window_sz" },
        {  7, 0, "tcp_stat.rtt_trip" },
        {  8, 0, "tcp_astat.cur_off" },
        {  9, 0, "tcp_stat.same_ack" },
        { 10, FLAG_IS_SLOW_TICKS,
          /**/   "tcp_stat.rtt_est" },
        { -1, 0, 0 },
      };
        
      for (k = 1; lcl_reg_names[k].index != -1; k++)
        {
          int index = lcl_reg_names[k].index;
          uint32_t val = c_val[index];
          uint32_t diff = val - prev_val[index];
                
          if (lcl_reg_names[k].flags & FLAG_IS_SLOW_TICKS)
        {
          printf("%-20s 0x%8x = %10u = %.1f us\n",
             lcl_reg_names[k].name,
             val, val,
             val *
             slow_tick_time * 1.e6);
        }
          else if (lcl_reg_names[k].flags & FLAG_IS_DIFFABLE)
        {
          printf("%-20s 0x%8x = %10u ; %10u\n",
             lcl_reg_names[k].name,
             val, val, diff);
        }
          else
        {
          printf("%-20s 0x%8x = %10u\n",
             lcl_reg_names[k].name,
             val, val);
        }
        }

      for (k = 0; k < NUM_DEBUG_REGISTERS; k++)
        {
          prev_val[k] = c_val[k];
        }
    }
    printf ("\n");
    {
      const index_name lcl_count_names[] = {
        {  0, 0, "idle_cnt" },
        { 26, 0, "slow_tick" },
        { 27, 0, "timeout_tick" },
        { 58, FLAG_WORDS_DIV_32,
          /* */  "in_words_div_32" },
        {  1, 0, "+-in_info.start_packet" },
        {  3, 0, "| `-in_info.start_arp" },
        {  4, 0, "|   in_info.arp_our_ip" },
        {  8, 0, "|   in_info.good_arp" },
        {  2, 0, "`-in_info.mac_for_us" },
        {  5, 0, "  in_info.start_ipv4" },
        {  6, 0, "  in_info.ip_hdr_ok" },
        {  7, 0, "  in_info.ip_for_us" },
        {  9, 0, "  +-in_info.start_icmp" },
        { 12, 0, "  | in_info.good_icmp" },
        { 10, 0, "  +-in_info.start_udp" },
        { 15, 0, "  | in_info.udp_arm" },
        { 16, 0, "  | in_info.udp_badactivearm" },
        { 17, 0, "  | in_info.udp_reset" },
        { 18, 0, "  | in_info.udp_badreset" },
        { 19, 0, "  | in_info.udp_disconnect" },
        { 20, 0, "  | in_info.udp_baddisconnect" },
        { 21, 0, "  | in_info.udp_regaccess" },
        { 22, 0, "  | +-in_info.udp_ra_otherip" },
        { 23, 0, "  |   +-in_info.udp_ra_seqplus1" },
        { 24, 0, "  |   +-in_info.udp_ra_repeat" },
        { 25, 0, "  |   `-in_info.udp_ra_busy" },
        { 60, 0, "  | in_info.udp_regaccess_idp" },
        { 61, 0, "  | `-in_info.udp_ra_idp_busy" },
        { 13, 0, "  | in_info.good_udp" },
        { 11, 0, "  `-in_info.start_tcp" },
        { 14, 0, "    in_info.good_tcp" },
          
        { 59, FLAG_WORDS_DIV_32,
          /* */  "out_words_div_32" },
        { 44, 0, "out_info.packets" },
        { 33, 0, "+-out_info.arp_icmp" },
        { 62, 0, "+-out_info.udp_idp" },
        { 34, 0, "+-out_info.udp[0]" },
        { 35, 0, "+-out_info.udp[1]" },
        { 36, 0, "+-out_info.udp[2]" },
        { 37, 0, "+-out_info.udp[3]" },
        /* 38 39 40 41 42 */
        { 43, 0, "+-out_info.tcp" },

        { 55, 0, "tcp_state.got_syn" },
        { 54, 0, "tcp_state.connected" },
          
        { 49, 0, "tcp_state.got_ack" },
        { 51, 0, "+-tcp_state.same_ack" },
        { 52, 0, "  +-tcp_state.twice_same_ack" },

        { 48, 0, "tcp_state.did_repeat" },
        { 53, 0, "tcp_state.abort_repeat" },
        { 57, 0, "tcp_state.did_keepalive" },

        { 47, 0, "+-tcp_state.start_meas_rtt" },
        { 50, 0, "  +-tcp_state.got_meas_rtt" },
        { 56, 0, "    +-tcp_state.new_rtt_est" },
          
        { -1, 0, 0 },
      };
        
      for (k = 0; lcl_count_names[k].index != -1; k++)
        {
          int index = lcl_count_names[k].index;
          uint32_t cnt = c_cnt[index];
          uint32_t diff = cnt - prev_cnt[index];

          printf("[%2d] %-33s %10u ; %10u\n",
             index, lcl_count_names[k].name, cnt, diff);

          if (lcl_count_names[k].flags & FLAG_WORDS_DIV_32)
        {
          uint64_t cnt64 = cnt;
          uint64_t diff64 = diff;

          /* Prescaled by factor 32, *2 for words -> bytes. */
          cnt64 *= 64;
          diff64 *= 64;
            
          printf("     %-30s "
             "%13" PRIu64 " ; %10" PRIu64"\n",
             "bytes",
             cnt64, diff64);
        }
        }

      for (k = 0; k < NUM_DEBUG_COUNTERS; k++)
        {
          prev_cnt[k] = c_cnt[k];
        }
    }
    printf("\n");
      
      }
      
      usleep(1000000);
    }
}

void fnet_udp_flood(struct fnet_ctrl_client *client,
            int items_per_packet)
{
  struct timeval t_prev, t_now;
  int packets;
  int ret;
  
  int prev_packets   = 0;
  int prev_timeout   = 0;
  int prev_malformed = 0;

  fnet_ctrl_client_stats *stats = fnet_ctrl_get_stats(client);

  timerclear(&t_prev);

  for (packets = 0; ; packets++)
    {
      fakernet_reg_acc_item *send;
      fakernet_reg_acc_item *recv;
      int num_send;
      int k;

      fnet_ctrl_get_send_recv_bufs(client, &send, &recv);
      
      for (k = 0; k < items_per_packet; k++)
    {
      send[k].addr =
        htonl(FAKERNET_REG_ACCESS_ADDR_READ | (1));
      send[k].data = htonl(0);
    }

      num_send = items_per_packet;

      ret = fnet_ctrl_send_recv_regacc(client, num_send);

      if (ret <= 0)
    {
      fprintf (stderr, "Failed: %s\n",
           fnet_ctrl_last_error(client));
      continue;
    }
    
      gettimeofday(&t_now, NULL);

      if (t_now.tv_sec <  t_prev.tv_sec ||
      t_now.tv_sec >= t_prev.tv_sec + 10)
    {
      double elapsed;
      int diff_packets = packets - prev_packets;

      int diff_timeout =
        stats->recv_timeout - prev_timeout;
      int diff_malformed =
        stats->malformed_packet - prev_malformed;

      elapsed = (t_now.tv_sec - t_prev.tv_sec) +
        0.000001 * (t_now.tv_usec - t_prev.tv_usec);

      printf("packets: %d, "
         "incr: %d (%.0f/s ; %.1f us/pkt) "
         "%.2f MB/s, "
         "(to: %d, mf: %d, to_us: %d)"
         "\n",
         packets,
         diff_packets, diff_packets/elapsed,
         elapsed / diff_packets * 1.e6,
         items_per_packet*4*diff_packets/elapsed/1.e6,
         diff_timeout, diff_malformed,
         fnet_ctrl_get_udp_timeout_usec(client));
      t_prev = t_now;
      prev_packets   = packets;
      prev_timeout   = stats->recv_timeout;
      prev_malformed = stats->malformed_packet;
    }
    }
}

void fnet_btn2led(struct fnet_ctrl_client *client)
{
  uint32_t rgb_out = 0;
  uint32_t rgb_cnt = 0;
  int ret;

  for ( ; ; )
    {
      fakernet_reg_acc_item *send;
      fakernet_reg_acc_item *recv;
      int num_send;

      uint32_t btn;
      static uint32_t prevbtn = (uint32_t) -1;
      int nbtn = 0;

      fnet_ctrl_get_send_recv_bufs(client, &send, &recv);   

      send[0].addr =
    htonl(FAKERNET_REG_ACCESS_ADDR_READ | (1));
      send[0].data = htonl(0);

      send[1].addr =
    htonl(FAKERNET_REG_ACCESS_ADDR_WRITE | (2));
      send[1].data = htonl(rgb_out);
      send[2].addr =
    htonl(FAKERNET_REG_ACCESS_ADDR_WRITE | (3));
      send[2].data = htonl(0x0800);

      num_send = 3;

      ret = fnet_ctrl_send_recv_regacc(client, num_send);

      if (ret <= 0)
    {
      fprintf (stderr, "Failed: %s\n",
           fnet_ctrl_last_error(client));
      continue;
    }

      btn = ntohl(recv[0].data);

      if (btn != prevbtn)
    {     
      printf("BTN: %08x = %d%d%d%d%d%d%d%d\n",
         btn,
         !!(btn & 0x01), !!(btn & 0x02),
         !!(btn & 0x04), !!(btn & 0x08),
         !!(btn & 0x10), !!(btn & 0x20),
         !!(btn & 0x40), !!(btn & 0x80));
      prevbtn = btn;
    }

      nbtn =
    !!(btn & 0x01) + !!(btn & 0x02) + !!(btn & 0x04) + !!(btn & 0x08) +
    !!(btn & 0x10) + !!(btn & 0x20) + !!(btn & 0x40) + !!(btn & 0x80);

      rgb_cnt++;

      rgb_out =
    ((rgb_cnt & 0x10) ? 0x008 : 0) |
    ((rgb_cnt & 0x20) ? 0x080 : 0) |
    ((rgb_cnt & 0x40) ? 0x800 : 0) |
    ((nbtn >= 1) ? 0x001 : 0) |
    ((nbtn >= 2) ? 0x002 : 0) |
    ((nbtn >= 3) ? 0x004 : 0) |
    ((nbtn >= 4) ? 0x010 : 0) |
    ((nbtn >= 5) ? 0x020 : 0) |
    ((nbtn >= 6) ? 0x040 : 0) |
    ((nbtn >= 7) ? 0x100 : 0) |
    ((nbtn >= 8) ? 0x200 : 0);

      usleep(10000);
    }
}

#define NUM_TEST_REGISTERS 5

void fnet_set_test_reg(struct fnet_ctrl_client *client,
               uint32_t testreg_addr,
               uint32_t testreg_value)
{
  fakernet_reg_acc_item *send;
  fakernet_reg_acc_item *recv;
  int num_send;
  int ret;

  uint32_t testreg[NUM_TEST_REGISTERS];
  int k;

  uint32_t commit_chance;
  uint32_t commit_lenmask;
  uint32_t commit_lenmax;

  fnet_ctrl_get_send_recv_bufs(client, &send, &recv);
    
  send[0].addr =
    htonl(FAKERNET_REG_ACCESS_ADDR_WRITE | testreg_addr);
  send[0].data = htonl(testreg_value);
    
  for (k = 0; k < NUM_TEST_REGISTERS; k++)
    {
      send[1+k].addr =
    htonl(FAKERNET_REG_ACCESS_ADDR_READ |
          (FAKERNET_REG_ACCESS_ADDR_INT_TEST_CTRL_REG + k));
      send[1+k].data = htonl(0);
    }   

  num_send = (1+NUM_TEST_REGISTERS);

  ret = fnet_ctrl_send_recv_regacc(client, num_send);

  if (ret <= 0)
    {
      fprintf (stderr, "Failed: %s\n",
           fnet_ctrl_last_error(client));
      exit(1);
    }

  for (k = 0; k < NUM_TEST_REGISTERS; k++)
    testreg[k] = ntohl(recv[1+k].data);

  commit_chance  = testreg[3];
  commit_lenmask = (testreg[4] >> 8) & 0xff;
  commit_lenmax  = testreg[4] & 0xff;

  double commit_prob;
  double commit_avg_len;
  uint32_t i;

  /* Probability that a commit is attempted. */
  commit_prob = (commit_chance / (double) 0xffffffff);

  /* Simply evaluate each possibility. */
  for (i = 0; i <= 255; i++)
    {
      uint32_t try_len = i & commit_lenmask;

      if (try_len &&
      try_len < commit_lenmax)
    {
      commit_avg_len += try_len;
    }
    }
  commit_avg_len /= 256;
  
  printf ("max-payload: %d  max-window: %d\n",
      testreg[0], testreg[1]);
  printf ("do_local: %d  "
      "commit-chance: %d  commit-max/mask: 0x%02x/0x%02x  "
      "%.3f kB/s\n",
      testreg[2],
      commit_chance,
      commit_lenmax, commit_lenmask,
      commit_prob *
      commit_avg_len *
      4 /* units of 4 bytes */ * 100.e6 /* operating freq */ / 1e3 /* kB*/);
}

void fnet_tcp_reset(struct fnet_ctrl_client *client)
{
  int ret;
  
  ret = fnet_ctrl_reset_tcp(client);

  if (ret < 0)
    {
      fprintf (stderr, "Failed: %s\n",
           fnet_ctrl_last_error(client));
      exit(1);
    }    
}

void fnet_tcp_read(struct fnet_ctrl_client *client,
           int local_verify)
{
  int tcp_sockfd;

  tcp_sockfd = fnet_ctrl_open_tcp(client);

  if (tcp_sockfd < 0)
    {
      fprintf (stderr, "Failed: %s\n",
           fnet_ctrl_last_error(client));
      exit(1);
    }

  fnet_ctrl_close(client);
  
  /*
    {
    int window = 1000;
    setsockopt(tcp_sockfd, IPPROTO_TCP, TCP_WINDOW_CLAMP,
    &window, sizeof(window));
    }
  */

#define TCP_BUF_SIZE 0x20000

  {
    char buffer[TCP_BUF_SIZE];
    size_t fill = 0;
    size_t total = 0;
    size_t total_prev = 0;
    struct timeval a, b;
    double infinite_response_avg = 0;
    /* For local-gen data verification: */
    uint32_t last_seq = (uint32_t) -1;
    uint32_t last_words = 0xff;
    uint32_t last_header = (uint32_t) -1;
    uint32_t word_count = 0;

    gettimeofday(&a, NULL);
        
    for ( ; ; ) {
    size_t todo = TCP_BUF_SIZE - fill;

    ssize_t n = read(tcp_sockfd, buffer+fill, todo);

    if (n == -1)
      {
        perror("read");
        return;
      }

    if (n == 0)
      {
        fprintf(stderr,"read got 0\n");
        return;
      }

    fill += n;
    total += n;

    if (local_verify)
      {
        uint32_t *p = (uint32_t *) buffer;
        uint32_t *ok = p;
        
        for ( ; ; )
          {
        uint32_t header;
        uint32_t words;
        uint32_t seq;
        uint32_t i;
        
        /* Do we have the full header word? */
        if (fill < sizeof (uint32_t))
          break;

        header = ntohl(*p);

        if ((header & 0xff000000) != 0xa5000000)
          {
            fprintf (stderr,
                 "Header word (0x%08x) has wrong marker.\n",
                 header);

            fprintf (stderr, "last header: 0x%08x\n", last_header);

            for (size_t j = 0; j < 32; j++)
              {
            printf ("%08x %2d: %08x\n",
                (uint32_t) (p-(uint32_t*)buffer),
                (int) j, ntohl(*(p+j)));
              }
            exit(1);
          }

        words = (header >> 16) & 0xff;

        if (words == 0)
          {
            fprintf (stderr,
                 "Header word (0x%08x) has wrong length.\n",
                 header);
            exit(1);
          }

        if (((header >> 8) & 0xff) != last_words)
          {
            fprintf (stderr,
                 "Header word (0x%08x) has "
                 "wrong prev length (0x%02x).\n",
                 header, last_words);
            exit(1);
          }

        /* Do we have all the data for this commit group? */
        if (fill < words * sizeof (uint32_t))
          break;

        seq = header & 0xff;

        if (seq != ((last_seq + 1) & 0xff))
          {
            fprintf (stderr,
                 "Header word (0x%08x) has "
                 "wrong seq (last=0x%04x).\n",
                 header, last_seq);
            exit(1);
          }

        last_seq = seq;
        last_words = words;
        last_header = header;

        p++;
        word_count++;

        /* Check the data words. */
        for (i = 1; i < words; i++)
          {
            uint32_t raw = ntohl(*(p++));

            if (raw != ((word_count & 0xffff) | ((~word_count) << 16)))
              {
            fprintf (stderr,
                 "Data word (0x%08x) wrong "
                 "count (expect 0x%04x).\n",
                 raw, word_count);
            exit(1);
              }

            word_count++;
          }

        fill -= words * sizeof (uint32_t);
        ok = p;
          }

        /* Move whatever data is left such that it can be checked
         * next time.
         */
        memmove(buffer, ok, fill);
      }
    else
      fill = 0;
    
    {
      struct timeval t_diff;
      double elapsed;
      double avg;

      gettimeofday(&b, NULL);

      timersub(&b, &a, &t_diff);

      elapsed = t_diff.tv_sec + 1.e-6 * t_diff.tv_usec;

      avg = (total - total_prev) / elapsed;

      if (elapsed > 1)
        {
          double fact = exp(-0.25 * elapsed);
        
          infinite_response_avg =
        (1 - fact) * avg +
        (    fact) * infinite_response_avg;

          printf("read: %zd (%.1f MB)  "
             "%.1f kB/s [slow avg %.1f kB/s]\r",
             total, total * 1.e-6,
             avg / 1000,
             infinite_response_avg / 1000);
          fflush(stdout);

          a = b;
          total_prev = total;
        }

      /* After having done one TCP_BUF_SIZE of data,
       * to emulate restricted bandwidth.
       */
      /* usleep(TCP_BUF_SIZE); */
    }

    /*
      {
      int one = 1;
      setsockopt(tcp_sockfd, IPPROTO_TCP, TCP_QUICKACK,
      &one, sizeof(one));
      }
    */
      }
  }
}

void fnet_ctrl_usage(char *cmdname)
{
  printf ("Fakernet control program.\n");
  printf ("\n");
  printf ("Usage: %s HOSTNAME(=IP-ADDR) <options>\n", cmdname);
  printf ("\n");
  printf ("  --stat           Query status counters once per second.\n");
  printf ("  --tcp-reset      Reset TCP connection.\n");
  printf ("  --tcp[=local]    Reset TCP, and read data (indefinately).\n");
  printf ("                   (local: verify local-generated data).\n");
  printf ("  --btn2led        Test buttons and LEDs on ARTY board.\n");
  printf ("  --tcp-payload=N  Restrict TCP packet payload size (0 = no restr.).\n");
  printf ("  --tcp-windows=N  Restrict TCP window size (0 = no restr.).\n");
  printf ("  --udp-flood=N    Flood UDP requests, with N regacc per request.\n");
  printf ("  --debug          Print debug messeages to stderr.\n");
  printf ("  --[no]idempotent  Use the first (no-sequence, multi-client) port.\n");
  printf ("  --help           Show this message.\n");
  printf ("\n");
}

int main(int argc, char **argv)
{
  struct fnet_ctrl_client *client;
  const char *error_ptr;

  char *hostname;
  int do_stat = 0;
  int do_tcp_reset = 0;
  int do_tcp_read = 0;
  int do_btn2led = 0;
  int do_udp_flood = 0;

  int items_per_packet;

  int do_testreg = 0;
  uint32_t testreg_addr = 0;
  uint32_t testreg_value = 0;

  int local_verify = 0;
  int reliable = -1;
  int debug = 0;
  int i;

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i],"--help") == 0)
    {
      fnet_ctrl_usage(argv[0]);
      exit(0);
    }

      if (strcmp(argv[i],"--debug") == 0)
    debug = 1;
      else if (strcmp(argv[i],"--reliable") == 0)
    reliable = 1;
      else if (strcmp(argv[i],"--noreliable") == 0)
    reliable = 0;
      else if (strcmp(argv[i],"--stat") == 0)
    do_stat = 1;
      else if (strcmp(argv[i],"--tcp-reset") == 0)
    do_tcp_reset = 1;
      else if (strcmp(argv[i],"--tcp") == 0)
    do_tcp_read = 1;
      else if (strcmp(argv[i],"--tcp=local") == 0)
    {
      do_tcp_read = 1;
      local_verify = 1;
    }
      else if (strcmp(argv[i],"--btn2led") == 0)
    do_btn2led = 1;
      else if (strncmp(argv[i],"--udp-flood=",12) == 0)
    {
      do_udp_flood = 1;
      items_per_packet = atoi(argv[i]+12);
    }
      else if (strncmp(argv[i],"--tcp-payload=",14) == 0)
    {
      do_testreg = 1;
      testreg_addr = FAKERNET_REG_ACCESS_ADDR_INT_TEST_CTRL_REG | 0;
      testreg_value = atoi(argv[i]+14);
    }
      else if (strncmp(argv[i],"--tcp-window=",13) == 0)
    {
      do_testreg = 1;
      testreg_addr = FAKERNET_REG_ACCESS_ADDR_INT_TEST_CTRL_REG | 1;
      testreg_value = atoi(argv[i]+13);
    }
      else if (strncmp(argv[i],"--tcp-lcl-datagen=",18) == 0)
    {
      do_testreg = 1;
      testreg_addr = FAKERNET_REG_ACCESS_ADDR_INT_TEST_CTRL_REG | 2;
      testreg_value = atoi(argv[i]+18);
    }
      else if (strncmp(argv[i],"--tcp-lcl-data-chance=",22) == 0)
    {
      do_testreg = 1;
      testreg_addr = FAKERNET_REG_ACCESS_ADDR_INT_TEST_CTRL_REG | 3;
      testreg_value = atoi(argv[i]+22);
    }
      else if (strncmp(argv[i],"--tcp-lcl-data-valmask=",23) == 0)
    {
      do_testreg = 1;
      testreg_addr = FAKERNET_REG_ACCESS_ADDR_INT_TEST_CTRL_REG | 4;
      testreg_value = atoi(argv[i]+23);
    }
      else
    {
      hostname = argv[i];
      /*
        fprintf (stderr, "Unhandled argument '%s'.\n", argv[i]);
        exit(1);
      */
    }
    }
  
  if (!hostname)
    {
      fnet_ctrl_usage(argv[0]);
      exit(1);
    }

  if (reliable == -1)
    {
      /* It was not specified if we should use a reliable or
       * the idempotent access channel.
       *
       * We only need the reliable channel for control access.
       * Also use it as default for UDP flood tests.
       */

      if (do_tcp_reset || do_tcp_read || do_testreg || do_udp_flood) {
    reliable = 1;
      } else {
    reliable = 0;
      }
    }
  
  if (0)
    {
      char debug_filename[256];

      sprintf (debug_filename,"udp_%ld_%d.dbg", (long) time(NULL), getpid());

      _fnet_debug_fid = fopen(debug_filename, "w");

      if (!_fnet_debug_fid)
    {
      printf ("Failed to open %s for debug output.\n", debug_filename);
      exit(1);
    }
      printf ("Debug output: %s\n", debug_filename);
    }

  client = fnet_ctrl_connect(hostname, reliable,
                 &error_ptr,
                 debug ? stderr : NULL);

  if (!client)
    {
      fprintf (stderr,
           "Failed to establish control connection "
           "to host %s: %s\n",
           hostname, error_ptr);
      exit(1);
    }

  /* The following sets registers once, and can then do another action. */
  
  if (do_testreg)
    fnet_set_test_reg(client, testreg_addr, testreg_value);

  if (do_tcp_reset)
    fnet_tcp_reset(client);
  
  /* The following routines never return. */

  if (do_tcp_read)
    fnet_tcp_read(client, local_verify);

  if (do_stat)
    fnet_ctrl_stat(client);
  
  if (do_btn2led)
    fnet_btn2led(client);
  
  if (do_udp_flood)
    fnet_udp_flood(client, items_per_packet);

  fnet_ctrl_close(client);
  
  return 0;
}
