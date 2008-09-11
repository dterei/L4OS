#include <clock/clock.h>
#include <clock/nslu2.h>
#include <l4/interrupt.h>
#include <stdio.h>

#include "pager.h"
#include "l4.h"
#include "libsos.h"
#include "syscall.h"

#define maybe(expr)\
	if (!(expr)) {\
		printf("!!! failed %s:%d\n", __FUNCTION__, __LINE__);\
		sos_print_error(L4_ErrorCode());\
	}\

#define verbose 0
#define TIMER_STACK_SIZE PAGESIZE

// See the ipx42x dev manual, chapter 14
#define OST_TS      ((uint32_t*) 0xc8005000)
#define OST_TIM0    ((uint32_t*) 0xc8005004)
#define OST_TIM0_RL ((uint32_t*) 0xc8005008)
#define OST_TIM0_RL ((uint32_t*) 0xc8005008)
#define OST_STS     ((uint32_t*) 0xC8005020)

#define CLEAR_ST   (1 << 2)
#define CLEAR_TIM0 (1)

#define ONESHOT_ENABLE 0x03

// number of times the timestamp has overflow
static timestamp_t ts_overflow = 0;

// list of blocked threads
typedef struct BlockedThread_t *BlockedThreads;
struct BlockedThread_t {
	L4_ThreadId_t tid;
	timestamp_t unblock;
	BlockedThreads next;
};

static BlockedThreads blocked_threads = NULL;


int start_timer(void) {
	dprintf(1, "*** start_timer\n");

	// Set up memory mapping for the timer registers, 1:1
	L4_Fpage_t fpage = L4_Fpage((L4_Word_t) OST_TS, PAGESIZE);
	L4_Set_Rights(&fpage, L4_ReadWriteOnly);

	L4_PhysDesc_t ppage = L4_PhysDesc((L4_Word_t) OST_TS, L4_UncachedMemory);
	L4_MapFpage(L4_rootspace, fpage, ppage);

	// Enable timestamp overflow interrupt
	*OST_STS |= CLEAR_ST;
	*OST_TS = 1;
	ts_overflow = 0;

	L4_LoadMR(0, NSLU2_TIMESTAMP_IRQ);
	maybe(L4_RegisterInterrupt(L4_rootserver, SOS_IRQ_NOTIFY_BIT, 0, 0));

	// Enable timestamp overflow interrupt
	*OST_STS |= CLEAR_TIM0;

	L4_LoadMR(0, NSLU2_TIMER0_IRQ);
	maybe(L4_RegisterInterrupt(L4_rootserver, SOS_IRQ_NOTIFY_BIT, 0, 0));

	return CLOCK_R_OK;
}

int register_timer(uint64_t delay, L4_ThreadId_t client) {
	dprintf(1, "*** register_timer: delay=%lld, client=%d\n",
			 delay, (int) L4_ThreadNo(client));
	timestamp_t ts = raw_time_stamp();
	timestamp_t cs = NSLU2_US2TICKS(delay);

	// Timer doesn't seem to wake up if initialised to 0
	if (cs == 0) cs = 1;

	// Add to a list of threads that want to be woken up,
	// and at what point?
	BlockedThreads bt = (BlockedThreads) malloc(sizeof(struct BlockedThread_t));
	bt->tid = client;
	bt->unblock = ts + cs;
	bt->next = blocked_threads;

	// Better to add at end probably, but more complicated so who cares
	blocked_threads = bt;

	// Next time to check might be sooner than we will already
	if (*OST_TIM0 == 0 || ((uint32_t) cs << 2) < *OST_TIM0) {
		dprintf(0, "reenabling\n");
		*OST_STS |= CLEAR_TIM0;
		*OST_TIM0_RL = ((uint32_t) cs) | ONESHOT_ENABLE;
	}

	return CLOCK_R_OK;
}

timestamp_t raw_time_stamp(void) {
	timestamp_t ts = 0L;
	ts += *OST_TS;
	ts += ts_overflow << 32;
	return ts;
}

timestamp_t time_stamp(void) {
	return NSLU2_TICKS2US(raw_time_stamp());
}

int stop_timer(void) {
	dprintf(1, "*** stop_timer\n");
	return CLOCK_R_OK;
}

int timestamp_irq(L4_ThreadId_t *tid, int *send) {
	dprintf(1, "*** received timestamp_irq\n");

	*OST_STS |= CLEAR_ST;
	ts_overflow++;

	return 1;
}

int timer_irq(L4_ThreadId_t *tid, int *send) {
	timestamp_t ts = raw_time_stamp();
	dprintf(1, "*** received timer_irq at %llu\n", ts);

	int delay = 0;
	uint32_t nextDelay = (uint32_t) (-1);

	// Figure out which threads need to be woken up
	for (BlockedThreads bt = blocked_threads; bt != NULL; bt = bt->next) {
		if (bt->unblock <= ts) {
			// Unblock thread
			syscall_reply(bt->tid);

			// Delete it from list
			if (bt->next != NULL) {
				bt->tid = bt->next->tid;
				bt->unblock = bt->next->unblock;
				bt->next = bt->next->next;
			} else {
				free(bt);
				blocked_threads = NULL;
			}
		} else {
			// Multiple blocking threads means we need to reenable
			// the tim0 after all this, but by how much?
			if (((uint32_t) (bt->unblock - ts)) < nextDelay) {
				delay = 1;
				nextDelay = (uint32_t) (bt->unblock - ts);
			}
		}
	}

	*OST_STS |= CLEAR_TIM0;

	// Adjust for time changing since last tested, since especially with
	// demand paging, it could have taken a while...
	timestamp_t ts2 = raw_time_stamp();
	nextDelay -= (ts2 - ts);
	if (nextDelay <= 0) nextDelay = 1;

	if (delay) {
		*OST_TIM0_RL = nextDelay | ONESHOT_ENABLE;
	} else {
		*OST_TIM0_RL = 0;
	}

	return 1;
}
