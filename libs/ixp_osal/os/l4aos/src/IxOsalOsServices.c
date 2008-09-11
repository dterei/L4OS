/**
 * @file IxOsalOsServices.c (l4aos)
 *
 * @brief Implementation for Irq, Mem, sleep. 
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

#include "ixp_osal/IxOsal.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <l4.h>		// from sos
#include <l4/interrupt.h>
#include <libsos.h>	// from sos

#include "ixp_osal/os/ixp400/IxOsalOsIxp425Irq.h"

typedef struct IxOsalIRQEntry
{
    void (*fRoutine) (void *);
    void *fParameter;
    uint32_t fFlags;
} IxOsalIRQEntry;

enum IxOsalIRQEntryFlags {
    kIxOsalIntOccurred = 0x00000001,
    kIxOsalIntEnabled  = 0x00000002,
    kIxOsalIntActive   = (kIxOsalIntOccurred|kIxOsalIntEnabled),
    kIxOsalIntReserved = 0x00000004,
};

#define NR_INTS	32
#define L4_INTERRUPT	((L4_Word_t) -1)

static char *traceHeaders[] = {
    "%lx",
    "%lx[fatal] ",
    "%lx[error] ",
    "%lx[warning] ",
    "%lx[message] ",
    "%lx[debug1] ",
    "%lx[debug2] ",
    "%lx[debug3] ",
    "%lx[all]"
};

/* by default trace all but debug message */
static IxOsalLogLevel sIxOsalCurrLogLevel = IX_OSAL_LOG_LVL_MESSAGE;
//static IxOsalLogLevel sIxOsalCurrLogLevel = IX_OSAL_LOG_LVL_ALL;

static IxOsalSemaphore sIxOsalMemAllocLock;
static IxOsalSemaphore sIxOsalLogLock;

static IxOsalSemaphore sIxOsalOsIrqLock;
static bool sIxOsalOsServicesInited;
static bool sIxOsalIntLocked, sIxOsalIntUnlocked;
static L4_Word_t sIxOsalMaxSysNum;

static IxOsalIRQEntry sIxOsalIrqInfo[NR_INTS];

// Only used from the IxOsalOsIxp400.c:ixOsalOemInit() routine
extern void ixOsalOSServicesInit(void);
void ixOsalOSServicesInit(void)
{
    if (sIxOsalOsServicesInited)
	return;

    sIxOsalOsServicesInited = true;
    
    sIxOsalMaxSysNum = 1;

    ixOsalSemaphoreInit(&sIxOsalOsIrqLock, 1);
    ixOsalSemaphoreInit(&sIxOsalMemAllocLock, 1);
    ixOsalSemaphoreInit(&sIxOsalLogLock, 1);
}

extern void ixOsalOSServicesFinaliseInit(void);
void ixOsalOSServicesFinaliseInit(void)
{
    // Block out timer1 & timestamp interrupts
    int i = sizeof(sIxOsalIrqInfo)/sizeof(sIxOsalIrqInfo[0]);
    do {
	if (!sIxOsalIrqInfo[--i].fRoutine)
	    sIxOsalIrqInfo[i].fFlags = kIxOsalIntReserved;
    } while (i);
}

/*
 * General interrupt handler
 */
extern int ixOsalOSServicesServiceInterrupt(L4_ThreadId_t *tP, int *sendP);
int ixOsalOSServicesServiceInterrupt(L4_ThreadId_t *tidP, int *sendP)
{
	int acknowledge = 0;
	L4_ThreadId_t fromTid = *tidP;

	L4_Word_t irqIndex = __L4_TCR_PlatformReserved(0);
	IxOsalIRQEntry *irq;

	ixOsalSemaphoreWait(&sIxOsalOsIrqLock, IX_OSAL_WAIT_FOREVER);
	//sos_logf("%s: got semaphore\n", __FUNCTION__);
	if (!L4_IsNilThread(fromTid)) {
		sIxOsalIntUnlocked = true;
	} else {
		// Interrupt message
		//sos_logf("%s: servicing IRQ %lu\n", __FUNCTION__, irqIndex);
		irq = &sIxOsalIrqInfo[irqIndex];
		assert(irqIndex < NR_INTS);
		assert( !(irq->fFlags & kIxOsalIntOccurred) );
		assert( !(irq->fFlags & kIxOsalIntReserved) );

		irq->fFlags |= kIxOsalIntOccurred;	// Interrupt occured

		// Call the interrupt routine if enabled
		if (sIxOsalIntLocked || !(irq->fFlags & kIxOsalIntEnabled)) {
			//sos_logf("%s: locked, won't ack\n", __FUNCTION__);
			;
		} else {
			//sos_logf("%s: calling service routine\n", __FUNCTION__);
			(*irq->fRoutine)(irq->fParameter);
			//sos_logf("%s: service routine done\n", __FUNCTION__);
			irq->fFlags &= ~kIxOsalIntOccurred;	// Clear occured bit
			acknowledge = 1;
		}
		*sendP = 0;
	}

	if (sIxOsalIntUnlocked) {
		//sos_logf("%s: got unlock msg\n", __FUNCTION__);
		// Must be an unlock message from user land not an interrupt
		// Probably should check the tag's label too
		for (L4_Word_t i = 0; i < NR_INTS; i++) {
			irq = &sIxOsalIrqInfo[i];
			// Check that the interrupt is enabled
			if (!irq->fRoutine
					||  (irq->fFlags & kIxOsalIntActive) != kIxOsalIntActive)
				continue;

			(*irq->fRoutine)(irq->fParameter);
			irq->fFlags &= ~kIxOsalIntOccurred;	// Clear occured bit

			if (irq->fFlags & kIxOsalIntEnabled) {
				// Acknowledge this interrupt now that it is enabled
				L4_Set_MsgTag(L4_Niltag); // Clear the tag down for sending
				L4_LoadMR(0, i);
				L4_Word_t succeeded = L4_AcknowledgeInterrupt(0, 0);
				if (!succeeded)
					sos_logf("%s: failed to ack IRQ %lu! error=%lu\n", __FUNCTION__, i, L4_ErrorCode());
			}
		}
		sIxOsalIntUnlocked = false;	// Done it
		*sendP = 1;
	}
	//sos_logf("%s: attempting to post semaphore\n", __FUNCTION__);
	ixOsalSemaphorePost(&sIxOsalOsIrqLock);
	return acknowledge;
}

/**************************************
 * Irq services 
 *************************************/

PUBLIC IX_STATUS
ixOsalIrqBind(UINT32 vector, IxOsalVoidFnVoidPtr routine, void *parameter)
{
    assert(sIxOsalOsServicesInited);
    IxOsalIRQEntry *irq = &sIxOsalIrqInfo[vector];

    assert( !(irq->fFlags & kIxOsalIntReserved) );
    if (vector >= NR_INTS || irq->fRoutine) {
        ixOsalLog(IX_OSAL_LOG_LVL_ERROR, IX_OSAL_LOG_DEV_STDOUT,
            "%s: illegal %d %p.\n", LOG_FUNCTION, vector,
	    (uintptr_t) irq->fRoutine, 0, 0, 0);
        return IX_FAIL;
    }

    irq->fRoutine   = routine;
    irq->fParameter = parameter;
    irq->fFlags     = kIxOsalIntEnabled;
    
    L4_LoadMR(0, vector);
    L4_Word_t succeeded = L4_RegisterInterrupt(L4_rootserver, SOS_IRQ_NOTIFY_BIT, 0, 0);
    if (!succeeded)
        sos_logf("%s: registering IRQ %lu failed! error=%lu\n", __FUNCTION__, vector, L4_ErrorCode());
    return IX_SUCCESS;
}

PUBLIC IX_STATUS
ixOsalIrqUnbind(UINT32 vector)
{
    assert(sIxOsalOsServicesInited);
    IxOsalIRQEntry *irq = &sIxOsalIrqInfo[vector];

    assert( !(irq->fFlags & kIxOsalIntReserved) );
    if (vector >= NR_INTS || !irq->fRoutine) {
        ixOsalLog (IX_OSAL_LOG_LVL_ERROR, IX_OSAL_LOG_DEV_STDOUT,
            "%s: illegal %d.\n", LOG_FUNCTION, vector,
	    (uintptr_t) irq->fRoutine, 0, 0, 0);
        return IX_FAIL;
    }
    else {
    L4_LoadMR(0, vector);
	L4_Word_t succeeded = L4_UnregisterInterrupt(L4_rootserver, 0, 0);
	if (!succeeded)
        sos_logf("%s: unregistering IRQ %lu failed! error=%lu\n", __FUNCTION__, vector, L4_ErrorCode());
	ixOsalSemaphoreWait(&sIxOsalOsIrqLock, IX_OSAL_WAIT_FOREVER);
	irq->fRoutine = NULL;
	irq->fFlags   = 0;
	ixOsalSemaphorePost(&sIxOsalOsIrqLock);
    }

    return IX_SUCCESS;
}

/* XXX gvdl: Doesn't disable thread scheduling */
PUBLIC UINT32
ixOsalIrqLock()
{
    UINT32 ret;

    assert(sIxOsalOsServicesInited);
    if (L4_IsThreadEqual(sos_my_tid(), L4_rootserver)) {
	ret = sIxOsalIntLocked;
	sIxOsalIntLocked = true;
	sIxOsalIntUnlocked = false;
    }
    else {
	ixOsalSemaphoreWait(&sIxOsalOsIrqLock, IX_OSAL_WAIT_FOREVER);
	ret = sIxOsalIntLocked;
	sIxOsalIntLocked = true;
	sIxOsalIntUnlocked = false;
	ixOsalSemaphorePost(&sIxOsalOsIrqLock);
    }

    return ret;
}

/* Enable interrupts and task scheduling,
 * input parameter: irqEnable status returned
 * by ixOsalIrqLock().
 */
PUBLIC void
ixOsalIrqUnlock(UINT32 lockKey)
{
    assert(sIxOsalOsServicesInited);

    if (L4_IsThreadEqual(sos_my_tid(), L4_rootserver)) {
	if (!lockKey && sIxOsalIntLocked)
	    sIxOsalIntUnlocked = true;
	sIxOsalIntLocked = lockKey;
    }
    else {
	ixOsalSemaphoreWait(&sIxOsalOsIrqLock, IX_OSAL_WAIT_FOREVER);
	bool reenable = sIxOsalIntLocked && !lockKey;
	if (reenable)
	    sIxOsalIntLocked = false;
	ixOsalSemaphorePost(&sIxOsalOsIrqLock);

	if (reenable) {
	    L4_MsgTag_t tag = L4_Niltag;
	    L4_Set_MsgTag(L4_MsgTagAddLabel(tag, L4_INTERRUPT));
	    tag = L4_Call(L4_rootserver);
	    assert(!L4_IpcFailed(tag));
	}
    }
}

// Not a supported function
PUBLIC UINT32
ixOsalIrqLevelSet(UINT32 level)
{
    assert(!"ixOsalIrqLevelSet");
    return 0;
}

static inline void register_int(L4_Word_t i)
{
    L4_LoadMR(0, i);
	L4_Word_t succeeded = L4_RegisterInterrupt(L4_rootserver, SOS_IRQ_NOTIFY_BIT, 0, 0);
	if (!succeeded)
        sos_logf("%s: enabling IRQ %lu failed! error=%lu\n", __FUNCTION__, i, L4_ErrorCode());
}

PUBLIC void
ixOsalIrqEnable(UINT32 irqLevel)
{
    assert(sIxOsalOsServicesInited);

    if (irqLevel >= NR_INTS) {
        // Not supported 
        ixOsalLog(IX_OSAL_LOG_LVL_MESSAGE, IX_OSAL_LOG_DEV_STDOUT,
            "%s: IRQ %d not supported\n", LOG_FUNCTION, irqLevel, 0, 0, 0, 0);
	return;
    }

    IxOsalIRQEntry *irq = &sIxOsalIrqInfo[irqLevel];
    if (L4_IsThreadEqual(sos_my_tid(), L4_rootserver)) {
        irq->fFlags |= kIxOsalIntEnabled;
        register_int(irqLevel);
    } else {
        ixOsalSemaphoreWait(&sIxOsalOsIrqLock, IX_OSAL_WAIT_FOREVER);
        bool reenable = !(irq->fFlags & kIxOsalIntEnabled);
        irq->fFlags |= kIxOsalIntEnabled;
        register_int(irqLevel);
        ixOsalSemaphorePost(&sIxOsalOsIrqLock);

        // Let the irq thread know
        if (reenable) {
            L4_MsgTag_t tag = L4_Niltag;
            L4_Set_MsgTag(L4_MsgTagAddLabel(tag, L4_INTERRUPT));
            tag = L4_Call(L4_rootserver);
            assert(!L4_IpcFailed(tag));
        }
    }
}

static inline void unregister_int(L4_Word_t i)
{
    L4_LoadMR(0, i);
	L4_Word_t succeeded = L4_UnregisterInterrupt(L4_rootserver, 0, 0);
	if (!succeeded)
        sos_logf("%s: disabling IRQ %lu failed! error=%lu\n", __FUNCTION__, i, L4_ErrorCode());
}

PUBLIC void
ixOsalIrqDisable(UINT32 irqLevel)
{
    assert(sIxOsalOsServicesInited);

    if (irqLevel < NR_INTS) {
        ixOsalSemaphoreWait(&sIxOsalOsIrqLock, IX_OSAL_WAIT_FOREVER);
        sIxOsalIrqInfo[irqLevel].fFlags &= ~kIxOsalIntEnabled;
        unregister_int(irqLevel);
        ixOsalSemaphorePost(&sIxOsalOsIrqLock);
    }
    else {
	// Not supported 
	ixOsalLog(IX_OSAL_LOG_LVL_MESSAGE, IX_OSAL_LOG_DEV_STDOUT,
	    "%s: IRQ %d not supported\n", LOG_FUNCTION, irqLevel, 0, 0, 0, 0);
    }
}

/*********************
 * Log function
 *********************/

INT32
ixOsalLog (IxOsalLogLevel level,
    IxOsalLogDevice device,
    char *format, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6)
{
    if (!sIxOsalLogLock)
	ixOsalOSServicesInit();
	
    INT32 ret = IX_OSAL_LOG_ERROR;

    ixOsalSemaphoreWait(&sIxOsalLogLock, IX_OSAL_WAIT_FOREVER);
    do {
	// Return -1 for custom display devices
	if (device != IX_OSAL_LOG_DEV_STDOUT
	&&  device != IX_OSAL_LOG_DEV_STDERR) {
	    printf("%s: only IX_OSAL_LOG_DEV_STDOUT and "
		   "IX_OSAL_LOG_DEV_STDERR are supported\n", __FUNCTION__);
	    break;
	}

	if (level <= sIxOsalCurrLogLevel && level != IX_OSAL_LOG_LVL_NONE) {
	    if (level == IX_OSAL_LOG_LVL_USER)
		ret = printf("%lx", L4_ThreadNo(sos_my_tid()));
	    else
		ret = printf(traceHeaders[level - 1], L4_ThreadNo(sos_my_tid()));

	    ret += printf(format, arg1, arg2, arg3, arg4, arg5, arg6);
	    break;
	}
    } while(0);
    ixOsalSemaphorePost(&sIxOsalLogLock);

    return ret;
}

PUBLIC UINT32
ixOsalLogLevelSet (UINT32 level)
{
    UINT32 oldLevel;

    // Check value first
    if (IX_OSAL_LOG_LVL_NONE <= level && level <= IX_OSAL_LOG_LVL_ALL) {
	oldLevel = sIxOsalCurrLogLevel;
	sIxOsalCurrLogLevel = level;
	return oldLevel;
    }
    else {
        ixOsalLog (IX_OSAL_LOG_LVL_MESSAGE, IX_OSAL_LOG_DEV_STDOUT,
            "ixOsalLogLevelSet: Log Level is between %d and %d \n",
            IX_OSAL_LOG_LVL_NONE, IX_OSAL_LOG_LVL_ALL, 0, 0, 0, 0);
        return IX_OSAL_LOG_LVL_NONE;
    }
}

/**************************************
 * Task services 
 *************************************/

PUBLIC void
ixOsalBusySleep(UINT32 microseconds)
{
    // never busy sleep on an L4 system
    assert(microseconds < 32 * 1000 * 1000);
    sos_usleep(microseconds);
}

PUBLIC void
ixOsalSleep (UINT32 milliseconds)
{
    assert(milliseconds < 32 * 1000);
    sos_usleep(milliseconds * 1000);
}

/**************************************
 * Memory functions 
 *************************************/

void *
ixOsalMemAlloc(UINT32 size)
{
    ixOsalSemaphoreWait(&sIxOsalMemAllocLock, IX_OSAL_WAIT_FOREVER);
    void *mem = malloc(size);
    ixOsalSemaphorePost(&sIxOsalMemAllocLock);
    return mem;
}

void
ixOsalMemFree(void *ptr)
{
    ixOsalSemaphoreWait(&sIxOsalMemAllocLock, IX_OSAL_WAIT_FOREVER);
    IX_OSAL_ASSERT(ptr);
    free(ptr);
    ixOsalSemaphorePost(&sIxOsalMemAllocLock);
}

/* 
 * Copy count bytes from src to dest ,
 * returns pointer to the dest mem zone.
 */
void *
ixOsalMemCopy(void *dest, void *src, UINT32 count)
{
    IX_OSAL_ASSERT(dest);
    IX_OSAL_ASSERT(src);
    return memcpy(dest, src, count);
}

/* 
 * Fills a memory zone with a given constant byte,
 * returns pointer to the memory zone.
 */
void *
ixOsalMemSet(void *ptr, UINT8 filler, UINT32 count)
{
    IX_OSAL_ASSERT(ptr);
    return memset (ptr, filler, count);
}

/*****************************
 *
 *  Time
 *
 *****************************/

/* Retrieve current system time */
void
ixOsalTimeGet (IxOsalTimeval * ptime)
{
    assert(!"ixOsalTimeGet");
}

/* Timestamp is implemented in OEM */
PUBLIC UINT32
ixOsalTimestampGet (void)
{
    return IX_OSAL_OEM_TIMESTAMP_GET();
}

/* OEM-specific implementation */
PUBLIC UINT32
ixOsalTimestampResolutionGet (void)
{
    return IX_OSAL_OEM_TIMESTAMP_RESOLUTION_GET();
}

PUBLIC UINT32
ixOsalSysClockRateGet (void)
{
    return IX_OSAL_OEM_SYS_CLOCK_RATE_GET();
}

PUBLIC void
ixOsalYield (void)
{
    L4_Yield();
}


/*****************************
 *
 *  Memory mapping functions
 *
 *****************************/
void IxOsalOsMapMemory(IxOsalMemoryMap *map)
{
    L4_Word_t virt = IX_OSAL_IXP400_VIRT_OFFSET(map->physicalAddress);

    ixOsalLog (IX_OSAL_LOG_LVL_DEBUG1, IX_OSAL_LOG_DEV_STDOUT,
	"%s: Mapping(%lx,%lx) => %lx\n",
	LOG_FUNCTION, map->physicalAddress, map->size, virt, 0, 0);

    L4_Fpage_t targetFpage = L4_Fpage(virt, map->size);
    L4_Set_Rights(&targetFpage, L4_ReadWriteOnly);
    L4_PhysDesc_t phys = L4_PhysDesc(map->physicalAddress, L4_IOMemory);
    assert(L4_MapFpage(L4_rootspace, targetFpage, phys));
    map->virtualAddress = virt;
}

void IxOsalOsUnmapMemory(IxOsalMemoryMap *map)
{
    L4_Word_t virt = map->virtualAddress;
    map->virtualAddress = 0;
    L4_Fpage_t targetFpage = L4_Fpage(virt, map->size);
    assert(L4_UnmapFpage(L4_rootspace, targetFpage));
}


