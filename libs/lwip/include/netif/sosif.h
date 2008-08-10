/****************************************************************************
 *
 *      $Id: sosif.h,v 1.2 2003/09/10 11:36:22 benjl Exp $
 *      Copyright (C) 2002 Operating Systems Research Group, UNSW, Australia.
 *
 *      This file is part of the Mungi operating system distribution.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      version 2 as published by the Free Software Foundation.
 *      A copy of this license is included in the top level directory of
 *      the Sos distribution.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 ****************************************************************************/

#ifndef LWIP_SOSIF_H
#define LWIP_SOSIF_H

#include "lwip/netif.h"

extern void sosIfInit(struct netif *netif);
extern void sosIfSetMacAddr(int portId, const uint8_t hwaddr[6]);

#endif /* LWIP_SOSIF_H */
