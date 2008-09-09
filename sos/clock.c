#include <clock/clock.h>
#include <stdio.h>

#include "l4.h"
#include "libsos.h"

#define verbose 2

#define TIMER_STACK_SIZE 1024 // XXX

static L4_Word_t timerStack[TIMER_STACK_SIZE];
static void timerHandler(void) {
	dprintf(1, "hello from the timer handler!\n");

	// Just wait and deal with interrupts.  Will probably involve
	// checking against a list (or array) of blocked threads.
}

int start_timer(void) {
	dprintf(1, "*** start_timer\n");

	// Set up memory mapping for the timer registers?

	// The timer handler will accept timer interrupts and deal with them.
	L4_ThreadId_t timer = sos_thread_new(timerHandler, timerStack);
	(void) timer;

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

	// Hopefully pretty straightforward.  Read of register(s)?

	return (timestamp_t) 0;
}

int stop_timer(void) {
	dprintf(1, "*** stop_timer\n");

	// Kill thread or something?

	return CLOCK_R_OK;
}

