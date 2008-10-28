/****************************************************************************
 *
 *      $Id: $
 *
 *      Description:	Hack timer based on busy waiting.  Must be replaced
 *      		by a real timer somewhat later when a timer driver is
 *      		written in Milestone 4.
 *
 *      Author:		Godfrey van der Linden
 *
 ****************************************************************************/

#include <assert.h>
#include <stdint.h>

#include "frames.h"
#include "l4.h"
#include "libsos.h"
#include "process.h"
#include "timer.h"

#define verbose 1

#if !defined(NULL)
#define NULL ((void *) 0)
#endif

static L4_ThreadId_t utimer_tid;
static Process *utimer_p;

typedef struct utimer_entry {
	L4_Word_t fStart;
	L4_Word_t fDiff;
	L4_ThreadId_t fTid;
} utimer_entry_t;

static int processExpired(void *node, void *key) {
	utimer_entry_t *t = (utimer_entry_t *) node;
	L4_Word_t now = *((L4_Word_t *) key);
	if (t->fDiff <= (now - t->fStart)) {
		L4_Reply(t->fTid);
		return 1;	
	}
	return 0;
}

static void
utimer(void)
{
	L4_KDB_SetThreadName(sos_my_tid(), "utimer");
	L4_Accept(L4_UntypedWordsAcceptor);

	List *entryq;
	entryq = list_empty();

	for (;;) {
		L4_Yield();

		// Walk the timer list
		L4_Word_t now = L4_KDB_GetTick();
		list_delete(entryq, processExpired, &now);

		// Wait for a new packet either blocking or non-blocking
		L4_MsgTag_t tag = L4_Niltag;
		if (list_null(entryq))
			L4_Set_ReceiveBlock(&tag);
		else
			L4_Clear_ReceiveBlock(&tag);

		L4_ThreadId_t wait_tid = L4_nilthread;
		tag = L4_Ipc(L4_nilthread, L4_anythread, tag, &wait_tid);

		if (!L4_IpcFailed(tag)) {
			// Received a time out request queue it
			L4_Msg_t msg; L4_MsgStore(tag, &msg);	// Get the message
			utimer_entry_t *entry = (utimer_entry_t *) L4_MsgWord(&msg, 0);
			entry->fTid  = wait_tid;
			list_shift(entryq, entry);
		}
		else if (3 == L4_ErrorCode()) // Receive error # 1
			continue;	// no-partner - non-blocking
		else
			assert(!"Unhandled IPC error");
	}
}

void
utimer_init(void)
{
	if (utimer_p != NULL) {
		dprintf(0, "!!! utimer_init: timer already initialised!");
		return;
	}

	// Start the idler
	utimer_p = process_run_rootthread("utimer", utimer, NO_TIMESTAMP, 0);
	utimer_tid = process_get_tid(utimer_p);
}

// 10 millisecond ticks
#define US_TO_TICKS(us)		((us) / (10 * 1000))
void
utimer_sleep(uint32_t microseconds)
{
	utimer_entry_t entry =
	{ L4_KDB_GetTick(), (uint32_t) US_TO_TICKS(microseconds)};

	L4_Msg_t msg; L4_MsgClear(&msg);
	L4_MsgAppendWord(&msg, (uintptr_t) &entry);
	L4_MsgLoad(&msg);

	L4_Call(utimer_tid);
}

