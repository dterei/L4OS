/**
 * @file IxOsalOsServicesAsmRoutines.s (vxWorks)
 *
 * @brief  Fastmutex implementation.
 * 
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



#ifdef __XSCALE__

/*
 * System defines and include files
 */
#define _ASMLANGUAGE
#include <arch/arm/arm.h>

    .balign 4

.global ixOsalOemFastMutexTryLock

/* Quickest mutex. r0 must contain a local memory address.
   C prototype:
   IX_STATUS ixOsalOemFastMutexTryLock(IxOsalFastMutex *mutex);
   mutex must be a local memory address (use IxOsalFastMutex).
   *mutex must be either 0 or 1 when this function is called.
   if *mutex is 0, *mutex will be set to 1 and the function returns 0;
   else *mutex will stay 1 and the function returns 1. */
_ARM_FUNCTION(ixOsalOemFastMutexTryLock)
	mov		r1, #1
	swp		r2, r1, [r0] /* r2 <= {r0} and {r0} <= r1 */
	mov		r0, r2
	mov		pc, lr


#endif
