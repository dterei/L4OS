/**
 * @file IxOsalOsTypes.h
 *
 * @brief l4aos specific headers
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

#ifndef IxOsalOsTypes_H
#define IxOsalOsTypes_H

#include <stddef.h>
#include <stdint.h>
#include <l4/types.h>

typedef int64_t	    INT64;  /**< 64-bit signed integer */
typedef uint64_t    UINT64; /**< 64-bit unsigned integer */
typedef int32_t	    INT32;  /**< 32-bit signed integer */
typedef uint32_t    UINT32; /**< 32-bit unsigned integer */
typedef int16_t	    INT16;  /**< 16-bit signed integer */
typedef uint16_t    UINT16; /**< 16-bit unsigned integer */
typedef int8_t	    INT8;   /**< 8-bit signed integer */
typedef uint8_t	    UINT8;  /**< 8-bit unsigned integer */
typedef UINT32	    ULONG;  /**< alias for UINT32 */
typedef UINT16	    USHORT; /**< alias for UINT16 */
typedef UINT8	    UCHAR;  /**< alias for UINT8 */
typedef UINT32	    BOOL;   /**< alias for UINT32 */

#if defined (CONFIG_CPU_IXP46X) || defined (CONFIG_ARCH_IXP465)
#undef __ixp46X
#define __ixp46X
#endif /* CONFIG_CPU_IXP46X */

/* Default stack limit is 10 KB */
#define IX_OSAL_OS_THREAD_DEFAULT_STACK_SIZE  (10240) 

/* Maximum stack limit is 32 MB */
#define IX_OSAL_OS_THREAD_MAX_STACK_SIZE      (33554432)  /* 32 MBytes */

/* Default thread priority */
#define IX_OSAL_OS_DEFAULT_THREAD_PRIORITY    (90)

/* Thread maximum priority (0 - 255). 0 - highest priority */
#define IX_OSAL_OS_MAX_THREAD_PRIORITY	      (255)

#define IX_OSAL_OS_WAIT_FOREVER (-1L)  

#define IX_OSAL_OS_WAIT_NONE	0

#define IX_OSAL_OS_ATTRIBUTE_PACKED __attribute__((__packed__))

typedef L4_ThreadId_t IxOsalOsThread;

/* Semaphore handle */   
typedef struct semaphore *IxOsalOsSemaphore;

/* Mutex handle */
typedef struct mutex *IxOsalOsMutex;

/* 
 * Fast mutex handle - fast mutex operations are implemented in
 * native assembler code using atomic test-and-set instructions 
 */
typedef IxOsalOsMutex IxOsalOsFastMutex;

typedef struct
{
    UINT32 msgLen;     /* Message Length */
    UINT32 maxNumMsg;  /* max number of msg in the queue */
    UINT32 currNumMsg; /* current number of msg in the queue */
    INT8   msgKey;     /* key used to generate the queue */
    INT8   queueId;    /* queue ID */

} IxOsalOsMessageQueue;

#ifdef __linux	// Not a linux build
#undef __linux
#endif

// From linux list of processors required to compile
#define XSCALE 33
#define SIMSPARCSOLARIS 157	// Arbitrary number can't find

#if defined(CONFIG_SUBPLAT_IXP420)
#define __XSCALE__ XSCALE
#define CPU XSCALE
#define __ixp42X 1
#endif

#ifndef BIT
#define BIT(x) (1 << (x))
#endif

// #define IX_UNIT_TEST 1

#endif /* IxOsalOsTypes_H */
