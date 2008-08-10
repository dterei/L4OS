/**
 * @file IxOsalOsSemaphore.c (l4aos)
 *
 * @brief Implementation for semaphore and mutex.
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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <l4.h>		// from sos


#include "ixp_osal/IxOsal.h"

struct tid_list {
    L4_ThreadId_t fTid;
    struct tid_list *fNext;
};

struct mutex {
    L4_Word_t fHolder;
    L4_Word_t fNeeded;
    L4_Word_t fCount;
};

// In assembler file
extern IX_STATUS ixOsalFastMutexLock(IxOsalFastMutex *mutex);

struct semaphore {
    struct mutex fInterlock;
    IxOsalFastMutex fInterlockP;
    UINT32 fCount;
    struct tid_list *fQueue;
};

#define VALID_HANDLE(handle) do {					\
    if (!handle || !*handle) {						\
        ixOsalLog(IX_OSAL_LOG_LVL_ERROR, IX_OSAL_LOG_DEV_STDOUT,	\
            "%s(): NULL handle \n", LOG_FUNCTION,			\
            0, 0, 0, 0, 0);						\
	return IX_FAIL;							\
    }									\
} while(0)

PUBLIC IX_STATUS
ixOsalSemaphoreInit(IxOsalSemaphore *sid, UINT32 start_value)
{
    struct semaphore *sem = calloc(1, sizeof(struct semaphore));
    if (!sem) {
        ixOsalLog(IX_OSAL_LOG_LVL_ERROR, IX_OSAL_LOG_DEV_STDOUT,
            "ixOsalSemaphoreInit: fail to allocate for semaphore \n",
            0, 0, 0, 0, 0, 0);
        return IX_FAIL;
    }

    sem->fCount = start_value;
    sem->fQueue = NULL;
    sem->fInterlockP = &sem->fInterlock;
    *sid = sem;

    return IX_SUCCESS;
}

/**
 * DESCRIPTION: If the semaphore is 'empty', the calling thread is blocked. 
 *              If the semaphore is 'full', it is taken and control is returned
 *              to the caller. If the time indicated in 'timeout' is reached, 
 *              the thread will unblock and return an error indication. If the
 *              timeout is set to 'IX_OSAL_WAIT_NONE', the thread will never block;
 *              if it is set to 'IX_OSAL_WAIT_FOREVER', the thread will block until
 *              the semaphore is available. 
 *
 *
 */
static IX_STATUS
ixOsalSemaphoreWaitInternal(struct semaphore *sem, bool wait)
{
    IX_STATUS ixStatus = IX_SUCCESS;

    // Get the lock, decrement the count, add to queue, get out with count
    ixOsalFastMutexLock(&sem->fInterlockP);

    // Will need to block eventually
    int mycount = sem->fCount--;
    if (mycount <= 0) {
	if (!wait) {
	    sem->fCount++;
	    ixStatus = IX_FAIL;
	}
	else { // we are going to wait so add to queue
	    struct tid_list entry, **add;

	    // Find end of tid_list XXX why not use a STAIL list?
	    for (add = &(sem->fQueue); *add; add = &((*add)->fNext))
		; 

	    entry.fTid  = sos_my_tid();
	    entry.fNext = NULL;
	    *add = &entry;
	}
    }
    ixOsalFastMutexUnlock(&sem->fInterlockP);

    if (mycount <= 0 && ixStatus != IX_FAIL) {
	// We are going to wait, but anything can wake us up
	// This is a bad assumption we could wake on anything FIXME
	L4_ThreadId_t partner;
	L4_Wait(&partner);
    }

    return ixStatus;
}


#define US_TO_TICKS(us)		((us) * 3333ULL / 50ULL)

PUBLIC IX_STATUS
ixOsalSemaphoreWait (IxOsalOsSemaphore *sid, INT32 timeout)
{
    VALID_HANDLE(sid);

    struct semaphore *sem = *sid;
    UINT32 start, duration;

    if (timeout == IX_OSAL_WAIT_FOREVER)
	return ixOsalSemaphoreWaitInternal(sem, /* wait */ true);

    IX_STATUS ixStatus = ixOsalSemaphoreWaitInternal(sem, /* wait */ false);
    if (!timeout || ixStatus == IX_SUCCESS)
	return ixStatus;

    start = IX_OSAL_OEM_TIMESTAMP_GET();
    duration = US_TO_TICKS(timeout * 1000);
    do {
	ixOsalYield();
	ixStatus = ixOsalSemaphoreWaitInternal(sem, /* wait */ false);
    } while (ixStatus != IX_SUCCESS
	  && IX_OSAL_OEM_TIMESTAMP_GET() - start < duration);
    return ixStatus;
}

/* 
 * Attempt to get semaphore, return immediately,
 * no error info because users expect some failures
 * when using this API.
 */
PUBLIC IX_STATUS
ixOsalSemaphoreTryWait(IxOsalSemaphore *sid)
{
    return ixOsalSemaphoreWait(sid,  IX_OSAL_WAIT_NONE);
}

/**
 *
 * DESCRIPTION: This function causes the next available thread in the pend queue
 *              to be unblocked. If no thread is pending on this semaphore, the 
 *              semaphore becomes 'full'. 
 */
PUBLIC IX_STATUS
ixOsalSemaphorePost(IxOsalSemaphore *sid)
{
    VALID_HANDLE(sid);

    struct semaphore *sem = *sid;
    L4_ThreadId_t tid = { 0 };

    /*Get the lock, up the value, take the head, and get out*/
    ixOsalFastMutexLock(&sem->fInterlockP);
    if (sem->fQueue) {
	assert((int) sem->fCount < 0);
	// Hand the count over to the next thread
	struct tid_list *head = sem->fQueue;
	tid         = head->fTid;
	sem->fQueue = head->fNext; // Drop the node, it will be freed later
    }
    sem->fCount++;
    ixOsalFastMutexUnlock(&sem->fInterlockP);

    // Ping the waiting thread if any
    if (tid.raw) {
	L4_LoadMR(0, 0);	// Empty message
	L4_Send_Nonblocking(tid);
    }

    return IX_SUCCESS;
}

PUBLIC IX_STATUS
ixOsalSemaphoreGetValue (IxOsalSemaphore * sid, UINT32 * value)
{
    VALID_HANDLE(sid);
    assert(value);

    struct semaphore *sem = *sid;
    *value = sem->fCount;
    return IX_SUCCESS;
}

PUBLIC IX_STATUS
ixOsalSemaphoreDestroy (IxOsalSemaphore *sid)
{
    VALID_HANDLE(sid);
 
    free(*sid);
    *sid = 0;
    return IX_SUCCESS;
}

/****************************
 *    Mutex 
 ****************************/

PUBLIC IX_STATUS
ixOsalMutexInit (IxOsalMutex *mutexP)
{
    return ixOsalSemaphoreInit((IxOsalSemaphore *) mutexP, 1);
}

PUBLIC IX_STATUS
ixOsalMutexLock (IxOsalMutex * mutex, INT32 timeout)
{
    return ixOsalSemaphoreWait((IxOsalSemaphore *) mutex, timeout);
}

PUBLIC IX_STATUS
ixOsalMutexUnlock (IxOsalMutex * mutex)
{
    return ixOsalSemaphorePost((IxOsalSemaphore *) mutex);
}

/* 
 * Attempt to get mutex, return immediately,
 * no error info because users expect some failures
 * when using this API.
 */
PUBLIC IX_STATUS
ixOsalMutexTryLock (IxOsalMutex * mutex)
{
    return ixOsalMutexLock(mutex, IX_OSAL_WAIT_NONE);
}

PUBLIC IX_STATUS
ixOsalMutexDestroy (IxOsalMutex * mutex)
{
    return ixOsalSemaphoreDestroy((IxOsalSemaphore *) mutex);
}

PUBLIC IX_STATUS
ixOsalFastMutexInit(IxOsalFastMutex *mutex)
{
    struct mutex *mtx = calloc(1, sizeof(struct mutex));
    if (mtx) {
	*mutex = (IxOsalFastMutex) mtx;
	return IX_SUCCESS;
    }
    else {
        ixOsalLog(IX_OSAL_LOG_LVL_ERROR, IX_OSAL_LOG_DEV_STDOUT,
            "%s(): NULL mutex handle \n",
            LOG_FUNCTION, 0, 0, 0, 0, 0);
        return IX_FAIL;
    }
}

PUBLIC IX_STATUS
ixOsalFastMutexUnlock(IxOsalFastMutex *mutex)
{
    VALID_HANDLE(mutex);

    struct mutex *mtx = (struct mutex *) *mutex;
    mtx->fHolder = 0;

    if (mtx->fNeeded) {
	mtx->fNeeded = 0;
	L4_ThreadSwitch(L4_nilthread);
    }

    return IX_SUCCESS;
}

PUBLIC IX_STATUS
ixOsalFastMutexDestroy(IxOsalFastMutex *mutex)
{
    ixOsalFastMutexUnlock(mutex);
    free(*mutex);
    return IX_SUCCESS;
}
