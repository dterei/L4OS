/**
 * @file IxOsalOs.h
 *
 * @brief linux-specific defines 
 *
 * Design Notes:
 *
 * @par
 * IXP400 SW Release version 2.1
 * 
 * -- Copyright Notice --
 * 
 * @par
 * Copyright (c) 2001-2005, Intel Corporation.
 * All rights reserved.
 * 
 * @par
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * 
 * @par
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * 
 * @par
 * -- End of Copyright Notice --
 */

#ifndef IxOsalOs_H
#define IxOsalOs_H

#ifndef IX_OSAL_CACHED
#error "Uncached memory not supported in linux environment"
#endif

#include <l4.h>		// From sos
#include <libsos.h>	// From sos
#include <stdio.h>

/**
 * l4aos implementations of macros.
 */

// XXX gvdl: One to one mapping
#define IX_OSAL_OS_MMU_VIRT_TO_PHYS(addr) (addr)
#define IX_OSAL_OS_MMU_PHYS_TO_VIRT(addr) (addr)

// Implemented in IxOsalOsServices.c
extern void IxOsalOsMapMemory(IxOsalMemoryMap *map);
extern void IxOsalOsUnmapMemory(IxOsalMemoryMap *map);

static inline void ixOsalOsCacheInvalidate(L4_Word_t addr, L4_Word_t size)
    { L4_CacheFlushRangeInvalidate(L4_rootspace, addr, addr + size); }
#define IX_OSAL_OS_CACHE_INVALIDATE(addr, size) \
	ixOsalOsCacheInvalidate((L4_Word_t) addr, (L4_Word_t) size)

static inline void ixOsalOsCacheFlush(L4_Word_t addr, L4_Word_t size)
    { L4_CacheFlushDRange(L4_rootspace, addr, addr + size); }
#define IX_OSAL_OS_CACHE_FLUSH(addr, size) \
	ixOsalOsCacheFlush((L4_Word_t) addr, (L4_Word_t) size)

/* Cache preload not available*/
#define IX_OSAL_OS_CACHE_PRELOAD(addr,size) do {} while(0)

#endif /* IxOsalOs_H */
