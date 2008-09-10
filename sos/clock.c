#include <clock/clock.h>
#include <clock/nslu2.h>
#include <l4/interrupt.h>
#include <stdio.h>

#include "pager.h"
#include "l4.h"
#include "libsos.h"

#define verbose 2
#define maybe(expr)\
	if (!(expr)) {\
		printf("!!! failed %s:%d\n", __FUNCTION__, __LINE__);\
		sos_print_error(L4_ErrorCode());\
	}\

#define TIMER_STACK_SIZE PAGESIZE

// See the ipx42x dev manual, chapter 14
#define OST_TS      ((uint32_t*) 0xc8005000)
#define OST_TIM0    ((uint32_t*) 0xc8005004)
#define OST_TIM0_RL ((uint32_t*) 0xc8005008)
#define OST_TIM0_RL ((uint32_t*) 0xc8005008)
#define OST_STS     ((uint32_t*) 0xC8005020)

static uint32_t cs_to_ms(uint32_t cs) {
	// The timer operates at 66.66 MHz - in other words,
	// 66.66 clock cycles per ms.
	// Unfortunately we don't appear to have floating
	// point arithmetic here, which is kind of odd.
	uint64_t ms = cs;
	ms *= 50;   // 3333 of these units per ms
	ms /= 3333; // ~1 of these units per ms :-)
	return (uint32_t) ms;
}

static L4_Word_t timerStack[TIMER_STACK_SIZE];
static void timerHandler(void) {
	dprintf(1, "*** timeHandler started\n");

	L4_ThreadId_t tid;
	L4_MsgTag_t tag;
	L4_Msg_t msg;
	int irq, irq_bit, irq_mask, notify_bits;

	// Associate with st interrupts
	irq = NSLU2_TIMESTAMP_IRQ;
	irq_bit = 24; // arbitrary, so long as it's not 31
	irq_mask = 1 << irq_bit;

	L4_LoadMR(0, irq);
	maybe(L4_RegisterInterrupt(sos_my_tid(), irq_bit, 0, 0));
	*OST_STS |= (1 << 2);

	// At the moment, just trying to receieve interrupt when the
	// system timer overflows
	int x;
	for(x = 0;;x++){printf("hello %d!\n", x);}
	for (;;) {
		// Accept interrupts
		L4_Set_NotifyMask(1 << irq_bit);
		L4_Accept(L4_NotifyMsgAcceptor);

		// Recieve interrupt data
		tag = L4_Wait(&tid);
		L4_MsgStore(tag, &msg);
		notify_bits = L4_MsgWord(&msg, 0);

		if (L4_IsNilThread(tid) && (notify_bits & irq_mask)) {
			irq = __L4_TCR_PlatformReserved(0);
			*OST_TS = 0;

			// Important stuff?
			printf("received: %x, %d\n", notify_bits, irq);

			//*OST_STS &= ~(1 << 2); // Clear ST interrupt bit
			*OST_STS |= (1 << 2); // Clear ST interrupt bit
			L4_LoadMR(0, irq);
			L4_AcknowledgeInterrupt(0, 0);
		} else {
			printf("got interrupt I didn't want\n");
		}
	}
}

int start_timer(void) {
	dprintf(1, "*** start_timer\n");

	// Set up memory mapping for the timer registers, 1:1
	L4_Fpage_t fpage = L4_Fpage((L4_Word_t) OST_TS, PAGESIZE);
	L4_Set_Rights(&fpage, L4_ReadWriteOnly);

	L4_PhysDesc_t ppage = L4_PhysDesc((L4_Word_t) OST_TS, L4_UncachedMemory);
	L4_MapFpage(L4_rootspace, fpage, ppage);

	// Start uptime timing
	*OST_TS = 0;

	// Start timer interrupt handler

#if 0
	L4_ThreadId_t timer = sos_get_new_tid();
	L4_ThreadId_t pager = L4_Pager();

	printf("sos_task_new at %ld (%ld), %ld, %p, %p\n",
			timer.raw, L4_ThreadNo(timer), pager.raw, timerHandler, timerStack);

	sos_task_new(L4_ThreadNo(timer), L4_Pager(),
			(void*) timerHandler, (void*) timerStack);

#else
	(void) timerHandler;
	(void) timerStack;

#endif

	return CLOCK_R_OK;
}

int register_timer(uint64_t delay, L4_ThreadId_t client) {
	dprintf(1, "*** register_timer: delay=%lld, client=%d\n",
			 delay, (int) L4_ThreadNo(client));

	// Add to a list of threads that want to be woken up,
	// and at what point?

	// For now, just reply.
	msgClear();
	L4_Reply(client);

	return CLOCK_R_OK;
}

timestamp_t time_stamp(void) {
	dprintf(1, "*** time_stamp\n");
	timestamp_t ts = 0L;

	// Issues with timestamp overflowing?
	ts |= cs_to_ms(*OST_TS);
	return ts;
}

int stop_timer(void) {
	dprintf(1, "*** stop_timer\n");

	// Kill thread or something?

	return CLOCK_R_OK;
}

