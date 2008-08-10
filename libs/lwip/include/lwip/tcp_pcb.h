/*
 * Copyright (c) 2001, Swedish Institute of Computer Science.
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 * 3. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 * $Id: tcp_pcb.h,v 1.1 2003/08/06 22:56:00 benjl Exp $
 */
#ifndef __LWIP_TCP_PCB_H__
#define __LWIP_TCP_PCB_H__

#include "lwip/sys.h"
#include "lwip/mem.h"

#include "lwip/pbuf.h"
#include "lwip/opt.h"
#include "lwip/ip.h"
#include "lwip/icmp.h"

#include "lwip/sys.h"

#include "lwip/err.h"

enum tcp_pcb_list{
  TCP_LIST_ACTIVE,
  TCP_LIST_TW,
  TCP_LIST_LISTEN,
  TCP_LIST_TMP
};

enum tcp_state {
  CLOSED      = 0,
  LISTEN      = 1,
  SYN_SENT    = 2,
  SYN_RCVD    = 3,
  ESTABLISHED = 4,
  FIN_WAIT_1  = 5,   
  FIN_WAIT_2  = 6,
  CLOSE_WAIT  = 7,
  CLOSING     = 8,
  LAST_ACK    = 9,
  TIME_WAIT   = 10
};

/* the TCP protocol control block */
struct tcp_pcb {
  struct tcp_pcb *next;   /* for the linked list */
  struct tcp_pcb *chain;  /* for the hashtable */

  enum tcp_state state;   /* TCP state */

  void *callback_arg;
  
  /* Function to call when a listener has been connected. */
  err_t (* accept)(void *arg, struct tcp_pcb *newpcb, err_t err);

  struct ip_addr local_ip;
  u16_t local_port;
  
  struct ip_addr remote_ip;
  u16_t remote_port;
  
  /* receiver varables */
  u32_t rcv_nxt;   /* next seqno expected */
  u32_t rcv_adv;   /* advertised top of window */
  u16_t rcv_wnd;   /* receiver window */

  /* Timers */
  u16_t tmr;

  /* Retransmission timer. */
  u8_t rtime;
  
  u16_t mss;   /* maximum segment size */

  u8_t flags;
#define TF_ACK_DELAY 0x01   /* Delayed ACK. */
#define TF_ACK_NOW   0x02   /* Immediate ACK. */
#define TF_INFR      0x04   /* In fast recovery. */
#define TF_RESET     0x08   /* Connection was reset. */
#define TF_CLOSED    0x10   /* Connection was sucessfully closed. */
#define TF_GOT_FIN   0x20   /* Connection was closed by the remote end. */
  
  /* RTT estimation variables. */
  u16_t rttest; /* RTT estimate in 500ms ticks */
  u32_t rtseq;  /* sequence number being timed */
  s32_t sa, sv;

  u16_t rto;    /* retransmission time-out */
  u8_t nrtx;    /* number of retransmissions */

  /* fast retransmit/recovery */
  u32_t lastack; /* Highest acknowledged seqno. */
  u8_t dupacks;
  
  /* congestion avoidance/control variables */
  u16_t cwnd;  
  u16_t ssthresh;

  /* sender variables */
  u32_t snd_nxt,       /* next seqno to be sent */
    snd_max,       /* Highest seqno sent. */
    snd_wnd,       /* sender window */
    snd_wl1, snd_wl2,
    snd_lbb;      

  u16_t snd_buf;   /* Avaliable buffer space for sending. */
  u8_t snd_queuelen;

  /* Function to be called when more send buffer space is avaliable. */
  err_t (* sent)(void *arg, struct tcp_pcb *pcb, u16_t space);
  u16_t acked;
  
  /* Function to be called when (in-sequence) data has arrived. */
  err_t (* recv)(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
  struct pbuf *recv_data;

  /* Function to be called when a connection has been set up. */
  err_t (* connected)(void *arg, struct tcp_pcb *pcb, err_t err);

  /* Function which is called periodically. */
  err_t (* poll)(void *arg, struct tcp_pcb *pcb);

  /* Function to be called whenever a fatal error occurs. */
  void (* errf)(void *arg, err_t err);
  
  u8_t polltmr, pollinterval;
  
  /* These are ordered by sequence number: */
  struct tcp_seg *unsent;   /* Unsent (queued) segments. */
  struct tcp_seg *unacked;  /* Sent but unacknowledged segments. */
#if TCP_QUEUE_OOSEQ  
  struct tcp_seg *ooseq;    /* Received out of sequence segments. */
#endif /* TCP_QUEUE_OOSEQ */

};

struct tcp_pcb_listen {  
  struct tcp_pcb_listen *next;   /* for the linked list */
  struct tcp_pcb_listen *chain;   /* for the hashtable */

  enum tcp_state state;   /* TCP state */

  void *callback_arg;
  
  /* Function to call when a listener has been connected. */
  void (* accept)(void *arg, struct tcp_pcb *newpcb);

  struct ip_addr local_ip;
  u16_t local_port;
};

void
tcp_pcb_init(void);

void
tcp_pcb_insert_active(struct tcp_pcb* pcb);

void
tcp_pcb_remove_active(struct tcp_pcb* pcb);

struct tcp_pcb *
tcp_pcb_find_active(u16_t l_port, u16_t r_port,
                    struct ip_addr l_addr, struct ip_addr r_addr);

void
tcp_pcb_insert_time_wait(struct tcp_pcb* pcb);

void
tcp_pcb_remove_time_wait(struct tcp_pcb* pcb);

struct tcp_pcb *
tcp_pcb_find_time_wait(u16_t l_port, u16_t r_port,
                       struct ip_addr l_addr, struct ip_addr r_addr);

void
tcp_pcb_insert_listen(struct tcp_pcb_listen* pcb);

void
tcp_pcb_remove_listen(struct tcp_pcb_listen* pcb);

struct tcp_pcb_listen *
tcp_pcb_find_listen(u16_t l_port, struct ip_addr l_addr);

/* The TCP PCB lists. */
extern struct tcp_pcb_listen *tcp_listen_pcbs;  /* List of all TCP PCBs in LISTEN state. */
extern struct tcp_pcb *tcp_active_pcbs;  /* List of all TCP PCBs that are in a
					    state in which they accept or send
					    data. */
extern struct tcp_pcb *tcp_tw_pcbs;      /* List of all TCP PCBs in TIME-WAIT. */

/*extern struct tcp_pcb *tcp_tmp_pcb;*/   /* Only used for temporary storage. */

/* Axoims about the above lists:   
   1) Every TCP PCB that is not CLOSED is in one of the lists.
   2) A PCB is only in one of the lists.
   3) All PCBs in the tcp_listen_pcbs list is in LISTEN state.
   4) All PCBs in the tcp_tw_pcbs list is in TIME-WAIT state.
*/

#endif /* __LWIP_TCP_PCB_H__ */
