#include <clock/clock.h>
#include <l4/ipc.h>
#include <stdio.h>

#define dprintf(v, args...) { if ((v) < verbose) printf(args); }
#define verbose 2

extern void msgClear(void);

int start_timer(void) {
	dprintf(1, "*** start_timer\n");

	return CLOCK_R_OK;
}

int register_timer(uint64_t delay, L4_ThreadId_t client) {
	dprintf(1, "*** register_timer: delay=%lld, client=%d\n",
			 delay, (int) L4_ThreadNo(client));

	msgClear();
	L4_Reply(client);

	return CLOCK_R_OK;
}

timestamp_t time_stamp(void) {
	dprintf(1, "*** time_stamp\n");

	return (timestamp_t) 0;
}

int stop_timer(void) {
	dprintf(1, "*** stop_timer\n");

	return CLOCK_R_OK;
}

