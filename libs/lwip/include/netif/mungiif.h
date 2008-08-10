/****************************************************************************
 *
 *      $Id: mungiif.h,v 1.1 2003/08/06 22:56:01 benjl Exp $
 *      Copyright (C) 2002 Operating Systems Research Group, UNSW, Australia.
 *
 *      This file is part of the Mungi operating system distribution.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      version 2 as published by the Free Software Foundation.
 *      A copy of this license is included in the top level directory of
 *      the Mungi distribution.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 ****************************************************************************/

#ifndef LWIP_MUNGIIF_H
#define LWIP_MUNGIIF_H

#include "lwip/netif.h"
#include "lwip.idl.h"

void mungiif_init(struct netif *);
void mungiif_init_timers(struct netif *);
void mungiif_input(struct netif *, cicap_t, pkt_seq);
void mungiif_iocomplete(clwip_t *, key_seq, bool);

#endif /* LWIP_MUNGIIF_H */
