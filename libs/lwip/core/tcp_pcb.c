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
 * $Id: tcp_pcb.c,v 1.4 2003/04/08 12:55:22 andrewb Exp $
 */

/* This file contains all functions for
 * allocation / deallocation /reallocation
 * of struct tcp_pcb-s
 */

#include "lwip/debug.h"
#include "lwip/inet.h"
#include "lwip/tcp.h"
#include "lwip/stats.h"

#include "lwip/arch/perf.h"

/* The TCP PCB Hashtables */
struct tcp_pcb *tcp_hash_active[TCP_HASH_ACTIVE_SIZE];
struct tcp_pcb *tcp_hash_tw[TCP_HASH_TW_SIZE];
struct tcp_pcb_listen *tcp_hash_listen[TCP_HASH_LISTEN_SIZE];

/* The TCP PCB lists. */
struct tcp_pcb_listen *tcp_listen_pcbs;	/* List of all TCP PCBs in 
					 * LISTEN state
					 */
struct tcp_pcb *tcp_active_pcbs;	/* List of all TCP PCBs that are 
					 * in a state in which they 
					 * accept or send data.
					 */
struct tcp_pcb *tcp_tw_pcbs;		/* List of all TCP PCBs in 
					 * TIME-WAIT.
					 */
/*struct tcp_pcb *tcp_tmp_pcb;*/

void
tcp_pcb_init(void){
	int i;

	DEBUGF(TCP_PCB_DEBUG, ("tcp_pcb_init called\n"));

	/* Clear globals. */
	tcp_listen_pcbs = NULL;
	tcp_active_pcbs = NULL;
	tcp_tw_pcbs = NULL;
	/*tcp_tmp_pcb = NULL;*/

	for(i=0; i<TCP_HASH_ACTIVE_SIZE; i++){
		tcp_hash_active[i] = NULL;
	}

	for(i=0; i<TCP_HASH_TW_SIZE; i++){
		tcp_hash_tw[i] = NULL;
	}

	for(i=0; i<TCP_HASH_LISTEN_SIZE; i++){
		tcp_hash_listen[i] = NULL;
	}
}

static uptr_t
tcp_pcb_hash2(u16_t l_port, struct ip_addr l_addr){
	uptr_t res = 0;
	res ^= (res<<16) ^ (uptr_t)l_addr.addr;
	res ^= (res<<16) ^ (uptr_t)l_port;

	return res;
}

static uptr_t
tcp_pcb_hash4(u16_t l_port, u16_t r_port, struct ip_addr l_addr,
	      struct ip_addr r_addr){
	uptr_t res = 0;
	res ^= (res<<16) ^ (uptr_t)l_addr.addr;
	res ^= (res<<16) ^ (uptr_t)r_addr.addr;
	res ^= (res<<16) ^ (uptr_t)l_port;
	res ^= (res<<16) ^ (uptr_t)r_port;

	return res;	
}

void
tcp_pcb_insert_active(struct tcp_pcb* pcb){
	uptr_t key;

	/* dodgy? */
	if (pcb->local_ip.addr == 0)
		pcb->local_ip = netif_default->ip_addr;

	DEBUGF(TCP_PCB_DEBUG, ("inserting act local(%lx:%d) remote(%lx:%d)\n",
		pcb->local_ip.addr, pcb->local_port, pcb->remote_ip.addr,
		pcb->remote_port));

	/* insert into the list */
	pcb->next = tcp_active_pcbs;
	tcp_active_pcbs = pcb;

	/* insert into the hashtable */
	key = tcp_pcb_hash4(pcb->local_port, pcb->remote_port, 
			    pcb->local_ip, pcb->remote_ip);

	key = key % TCP_HASH_ACTIVE_SIZE;

	pcb->chain = tcp_hash_active[key];
	tcp_hash_active[key] = pcb;
}

void
tcp_pcb_remove_active(struct tcp_pcb* pcb){
	uptr_t key;
	struct tcp_pcb *tmp;

	DEBUGF(TCP_PCB_DEBUG, ("removing act local(%lx:%d) remote(%lx:%d)\n",
		pcb->local_ip.addr, pcb->local_port, pcb->remote_ip.addr,
		pcb->remote_port));

	/* FIXME: O(n) removal still sucks */

	/* remove from the list */
	if( tcp_active_pcbs == pcb) {
		tcp_active_pcbs = pcb->next;
	} else for(tmp = tcp_active_pcbs; tmp != NULL; tmp = tmp->next){
		if(tmp->next == pcb){
			tmp->next = pcb->next;
			break;
		}
	}
        pcb->next = NULL;

	/* remove from the hashtable */
	key = tcp_pcb_hash4(pcb->local_port, pcb->remote_port, 
			    pcb->local_ip, pcb->remote_ip);

	key = key % TCP_HASH_ACTIVE_SIZE;

	if(tcp_hash_active[key]==pcb){
		tcp_hash_active[key]=pcb->chain;
	}else for(tmp = tcp_hash_active[key]; tmp!=NULL; tmp = tmp->chain){
		if(tmp->chain==pcb){
			tmp->chain = pcb->chain;
			break;
		}
	}
	pcb->chain = NULL;
}

struct tcp_pcb *
tcp_pcb_find_active(u16_t l_port, u16_t r_port,
		    struct ip_addr l_addr, struct ip_addr r_addr){
	uptr_t key;
	struct tcp_pcb *pcb;

	DEBUGF(TCP_PCB_DEBUG, ("finding act local(%lx:%d) remote(%lx:%d)\n",
		l_addr.addr, l_port, r_addr.addr, r_port));

	key = tcp_pcb_hash4(l_port, r_port, l_addr, r_addr);

	key = key % TCP_HASH_ACTIVE_SIZE;

	for(pcb = tcp_hash_active[key]; pcb!=NULL; pcb = pcb->chain){
		if(l_port==pcb->local_port &&
		   r_port==pcb->remote_port &&
		   l_addr.addr==pcb->local_ip.addr &&
		   r_addr.addr==pcb->remote_ip.addr){
			DEBUGF(TCP_PCB_DEBUG, ("found it\n"));
			return pcb;
		}
	}
	/* not found */
	DEBUGF(TCP_PCB_DEBUG, ("not found it\n"));
	return NULL;
}

void
tcp_pcb_insert_time_wait(struct tcp_pcb *pcb){
	uptr_t key;

	DEBUGF(TCP_PCB_DEBUG, ("inserting tw local(%lx:%d) remote(%lx:%d)\n",
		pcb->local_ip.addr, pcb->local_port, pcb->remote_ip.addr,
		pcb->remote_port));

	/* insert into the list */
	pcb->next = tcp_tw_pcbs;
	tcp_tw_pcbs = pcb;

	/* insert into the hashtable */
	key = tcp_pcb_hash4(pcb->local_port, pcb->remote_port, 
			    pcb->local_ip, pcb->remote_ip);

	key = key % TCP_HASH_TW_SIZE;

	pcb->chain = tcp_hash_tw[key];
	tcp_hash_tw[key] = pcb;	
}

void
tcp_pcb_remove_time_wait(struct tcp_pcb *pcb){
	uptr_t key;
	struct tcp_pcb *tmp;

	DEBUGF(TCP_PCB_DEBUG, ("removing act local(%lx:%d) remote(%lx:%d)\n",
		pcb->local_ip.addr, pcb->local_port, pcb->remote_ip.addr,
		pcb->remote_port));

	/* FIXME: O(n) removal still sucks */

	/* remove from the list */
	if( tcp_tw_pcbs == pcb) {
		tcp_tw_pcbs = pcb->next;
	} else for(tmp = tcp_tw_pcbs; tmp != NULL; tmp = tmp->next){
		if(tmp->next == pcb){
			tmp->next = pcb->next;
			break;
		}
	}
        pcb->next = NULL;

	/* remove from the hashtable */
	key = tcp_pcb_hash4(pcb->local_port, pcb->remote_port, 
			    pcb->local_ip, pcb->remote_ip);

	key = key % TCP_HASH_TW_SIZE;

	if(tcp_hash_tw[key]==pcb){
		tcp_hash_tw[key]=pcb->chain;
	}else for(tmp = tcp_hash_tw[key]; tmp!=NULL; tmp = tmp->chain){
		if(tmp->chain==pcb){
			tmp->chain = pcb->chain;
			break;
		}
	}
	pcb->chain = NULL;
}

struct tcp_pcb *
tcp_pcb_find_time_wait(u16_t l_port, u16_t r_port,
		       struct ip_addr l_addr, struct ip_addr r_addr){
	uptr_t key;
	struct tcp_pcb *pcb;

	DEBUGF(TCP_PCB_DEBUG, ("finding tw local(%lx:%d) remote(%lx:%d)\n",
		l_addr.addr, l_port, r_addr.addr, r_port));

	key = tcp_pcb_hash4(l_port, r_port, l_addr, r_addr);

	key = key % TCP_HASH_TW_SIZE;

	for(pcb = tcp_hash_tw[key]; pcb!=NULL; pcb = pcb->chain){
		if(l_port==pcb->local_port &&
		   r_port==pcb->remote_port &&
		   l_addr.addr==pcb->local_ip.addr &&
		   r_addr.addr==pcb->remote_ip.addr){
			DEBUGF(TCP_PCB_DEBUG, ("found it\n"));
			/* found it */
			return pcb;
		}
	}
	/* not found */
	DEBUGF(TCP_PCB_DEBUG, ("not found it\n"));
	return NULL;
}

void
tcp_pcb_insert_listen(struct tcp_pcb_listen *pcb){
	uptr_t key;

	DEBUGF(TCP_PCB_DEBUG, ("inserting listen local(%lx:%d)\n",
		pcb->local_ip.addr, pcb->local_port));

	/* insert into the list */
	pcb->next = tcp_listen_pcbs;
	tcp_listen_pcbs = pcb;

	/* insert into the hashtable */
	key = tcp_pcb_hash2(pcb->local_port, pcb->local_ip);

	key = key % TCP_HASH_LISTEN_SIZE;

	pcb->chain = tcp_hash_listen[key];
	tcp_hash_listen[key] = pcb;
}

void
tcp_pcb_remove_listen(struct tcp_pcb_listen *pcb){
	uptr_t key;
	struct tcp_pcb_listen *tmp;

	DEBUGF(TCP_PCB_DEBUG, ("removing listen local(%lx:%d)\n",
		pcb->local_ip.addr, pcb->local_port));

	/* FIXME: O(n) removal still sucks */

	/* remove from the list */
	if( tcp_listen_pcbs == pcb) {
		tcp_listen_pcbs = pcb->next;
	} else for(tmp = tcp_listen_pcbs; tmp != NULL; tmp = tmp->next){
		if(tmp->next == pcb){
			tmp->next = pcb->next;
			break;
		}
	}
        pcb->next = NULL;

	/* remove from the hashtable */
	key = tcp_pcb_hash2(pcb->local_port, pcb->local_ip);

	key = key % TCP_HASH_LISTEN_SIZE;

	if(tcp_hash_listen[key]==pcb){
		tcp_hash_listen[key]=pcb->chain;
	}else for(tmp = tcp_hash_listen[key]; tmp!=NULL; tmp = tmp->chain){
		if(tmp->chain==pcb){
			tmp->chain = pcb->chain;
			break;
		}
	}
	pcb->chain = NULL;
}

struct tcp_pcb_listen *
tcp_pcb_find_listen(u16_t l_port, struct ip_addr l_addr){
	uptr_t key;
	struct tcp_pcb_listen *pcb;

	DEBUGF(TCP_PCB_DEBUG, ("finding listen local(%lx:%d)\n",
		l_addr.addr, l_port));

	key = tcp_pcb_hash2(l_port, l_addr);

	key = key % TCP_HASH_LISTEN_SIZE;

	for(pcb = tcp_hash_listen[key]; pcb!=NULL; pcb = pcb->chain){
		if(l_port==pcb->local_port &&
		   l_addr.addr==pcb->local_ip.addr){
			DEBUGF(TCP_PCB_DEBUG, ("found it\n"));
			/* found it */
			return pcb;
		}
	}

	/* if we can't find it in the hashtable, we have to resort to a 
	 * linear search. This is to allow us to do a wildcard match on 
	 * the local address.
	 */
	DEBUGF(TCP_PCB_DEBUG, ("not found - doing wildcard match\n"));
	for(pcb=tcp_listen_pcbs; pcb!=NULL; pcb=pcb->next){
		DEBUGF(TCP_PCB_DEBUG, ("considering %lx:%d\n",
			pcb->local_ip.addr, pcb->local_port));
		if(ip_addr_isany(&(pcb->local_ip)) && l_port==pcb->local_port){
			DEBUGF(TCP_PCB_DEBUG, ("wildcard found it\n"));
			return pcb;
		}
	}

	/* not found */
	DEBUGF(TCP_PCB_DEBUG, ("wildcard not found\n"));
	return NULL;
}
