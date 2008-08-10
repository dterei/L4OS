/**
 * @file IxOsalOsThread.c (linux)
 *
 * @brief OS-specific thread implementation.
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

#include <stdlib.h>

#include <libsos.h>
#include "ixp_osal/IxOsal.h"

#define kStackSize IX_OSAL_OS_THREAD_DEFAULT_STACK_SIZE

/* Thread attribute is ignored */
PUBLIC IX_OSAL_INLINE BOOL
ixOsalThreadStopCheck()
{
    assert(!"ixOsalThreadStopCheck");
    return FALSE;
}

static void threadInternal(void)
{
    void **data = (void **) L4_UserDefinedHandle();
    void (*func)(void *) = data[0];
    void *arg            = data[1];

    L4_Set_UserDefinedHandle((L4_Word_t)data[2]);
    (*func)(arg);
    ixOsalThreadExit();
}

PUBLIC IX_STATUS
ixOsalThreadCreate(IxOsalThread * ptrTid,
    IxOsalThreadAttr * threadAttr, IxOsalVoidFnVoidPtr entryPoint, void *arg)
{
    L4_Word_t ip = (L4_Word_t) &threadInternal;
    L4_Word_t sp = (L4_Word_t) malloc(kStackSize);
    assert(sp && !(sp & (sizeof(void*) - 1)) ); // aligned?

    void **args = (void **) sp;
    args[0] = entryPoint;
    args[1] = arg;

    L4_ThreadId_t tid = sos_get_new_tid();
    args[2] = (void *)tid.raw;

    // Create active thread
    int res = L4_ThreadControl(tid,
			       L4_rootspace,	// address space
			       L4_rootserver,	// scheduler
			       L4_rootserver,	// pager
			       L4_rootserver,	// exception handler
			       0,	// resources
			       (void *) -1UL); //utcb
    if (!res) {
        ixOsalLog(IX_OSAL_LOG_LVL_ERROR, IX_OSAL_LOG_DEV_STDOUT,
            "%s(): failed\n", LOG_FUNCTION, 0, 0, 0, 0, 0);

	return IX_FAIL;
    }
    L4_ThreadId_t dummy_id;
    L4_Word_t control = L4_ExReg_sp_ip | L4_ExReg_user
		      | L4_ExReg_Halt  | L4_ExReg_AbortIPC;
    L4_ExchangeRegisters(tid, control, sp + kStackSize, ip, 0, sp,
	    L4_nilthread, &sp, &sp, &sp, &sp, &sp, &dummy_id);

    *ptrTid = tid;
    return IX_SUCCESS;
}

/* 
 * Start the thread
 */
PUBLIC IX_STATUS
ixOsalThreadStart(IxOsalThread *tid)
{
    L4_Start(*tid);
    return IX_SUCCESS;
}
	
/*
 * Kill the kernel thread. This shall not be used if the thread function
 * implements do_exit()
 */
PUBLIC IX_STATUS
ixOsalThreadKill(IxOsalThread *tidP)
{
    // xxx gvdl: Leaks the stack
    // Terminate the thread
    int res = L4_ThreadControl(*tidP,
			       L4_nilspace,	// address space
			       L4_nilthread,	// scheduler
			       L4_nilthread,	// pager
			       L4_nilthread,	// exception handler
			       0,	// resources
			       NULL);
    assert(res);
    return IX_SUCCESS;
}


PUBLIC void
ixOsalThreadExit(void)
{
    IxOsalThread tid = sos_my_tid();
    ixOsalThreadKill(&tid);
}

PUBLIC IX_STATUS
ixOsalThreadPrioritySet(IxOsalOsThread *tid, UINT32 priority)
{
    return IX_SUCCESS;
}

PUBLIC IX_STATUS
ixOsalThreadSuspend(IxOsalThread *tid)
{
    assert(!"ixOsalThreadSuspend");
    return IX_FAIL;
}

PUBLIC IX_STATUS
ixOsalThreadResume(IxOsalThread *tid)
{
    assert(!"ixOsalThreadResume");
    return IX_FAIL;
}
