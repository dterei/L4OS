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

#include "queue.h"
#include "libsos.h"

#include "l4.h"

#if !defined(NULL)
#define NULL ((void *) 0)
#endif

// APIs for lib sos
extern void utimer_init(void);
extern void utimer_sleep(uint32_t microseconds);

static uint8_t utimer_stack[1][256];
static L4_ThreadId_t utimer_tid_s;
typedef struct utimer_entry {
	LIST_ENTRY(utimer_entry) fChain;
	L4_Word_t fStart;
	L4_Word_t fDiff;
	L4_ThreadId_t fTid;
} utimer_entry_t;

static void utimer(void)
{
	L4_KDB_SetThreadName(sos_my_tid(), "utimer");
	L4_Accept(L4_UntypedWordsAcceptor);

	LIST_HEAD(, utimer_entry) entryq;
	LIST_INIT(&entryq);

	for (;;) {
		L4_Yield();

		// Walk the timer list
		utimer_entry_t *entry, *next;
		L4_Word_t now = L4_KDB_GetTick();
		LIST_FOREACH_SAFE(entry, &entryq, fChain, next) {

			// Has the timer expired?
			if (entry->fDiff <= (now - entry->fStart)) {
				LIST_REMOVE(entry, fChain);
				L4_Reply(entry->fTid);
			}
		}

		// Wait for a new packet either blocking or non-blocking
		L4_MsgTag_t tag = L4_Niltag;
		if (LIST_EMPTY(&entryq))
			L4_Set_ReceiveBlock(&tag);
		else
			L4_Clear_ReceiveBlock(&tag);

		L4_ThreadId_t wait_tid = L4_nilthread;
		tag = L4_Ipc(L4_nilthread, L4_anythread, tag, &wait_tid);

		if (!L4_IpcFailed(tag)) {
			// Received a time out request queue it
			L4_Msg_t msg; L4_MsgStore(tag, &msg);	// Get the message
			entry = (utimer_entry_t *) L4_MsgWord(&msg, 0);
			entry->fTid  = wait_tid;
			LIST_INSERT_HEAD(&entryq, entry, fChain);
		}
		else if (3 == L4_ErrorCode()) // Receive error # 1
			continue;	// no-partner - non-blocking
		else
			assert(!"Unhandled IPC error");
	}
}

void utimer_init(void)
{
	// Start the idler
	utimer_tid_s = sos_thread_new(&utimer, &utimer_stack[1]);
}

// 10 millisecond ticks
#define US_TO_TICKS(us)		((us) / (10 * 1000))
void utimer_sleep(uint32_t microseconds)
{
	utimer_entry_t entry =
	{ {0}, L4_KDB_GetTick(), (uint32_t) US_TO_TICKS(microseconds)};

	L4_Msg_t msg; L4_MsgClear(&msg);
	L4_MsgAppendWord(&msg, (uintptr_t) &entry);
	L4_MsgLoad(&msg);

	L4_Call(utimer_tid_s);
}
