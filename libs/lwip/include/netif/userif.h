/****************************************************************************
 *
 *      $Id: userif.h,v 1.1 2003/08/06 22:56:02 benjl Exp $
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

void userif_init(struct netif *);
void userif_input(struct netif *, void* buf, uintptr_t length, uintptr_t key);
void userif_complete(void* buf);

#endif /* LWIP_MUNGIIF_H */
