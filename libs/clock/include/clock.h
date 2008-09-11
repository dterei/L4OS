#ifndef _CLOCK_H_
#define _CLOCK_H_

#include <l4/types.h>
#include <stdint.h>

/*
 * Return codes for driver functions
 */
#define CLOCK_R_OK     0	/* success */
#define CLOCK_R_UINT (-1)	/* driver not initialised */
#define CLOCK_R_CNCL (-2)	/* operation cancelled (driver stopped) */
#define CLOCK_R_FAIL (-3)	/* operation failed for other reason */

typedef uint64_t timestamp_t;

/*
 * Initialise driver. Performs implicit stop_timer() if already initialised.
 *
 * Returns CLOCK_R_OK iff successful.
 */
int start_timer(void); 

/*
 * Register a client thread to be woken up by an IPC.
 *    delay:  Delay time in microseconds before sending wakeup IPC to client.
 *    client: Client id.
 *
 * Returns CLOCK_R_OK iff successful.
 */
int register_timer(uint64_t delay, L4_ThreadId_t client);  

/*
 * Returns present time in microseconds since booting.
 *
 * Returns a negative value if failure.
 */
timestamp_t raw_time_stamp(void);
timestamp_t time_stamp(void);

/*
 * Stop clock driver operation.
 *
 * Returns CLOCK_R_OK iff successful.
 */
int stop_timer(void);

/*
 * Callbacks.
 *
 * Returns whether to acknowledge interrupt or not.
 *
 * tid is the threadid of originating interrupt - should be
 * nil thread but gives the opportunity to change (for
 * whatever reason - network_irq needs it).  Similarly
 * for send.  The expectation is that neither will change.
 */
int timestamp_irq(L4_ThreadIt_t *tid, int *send);
int timer_irq(L4_ThreadIt_t *tid, int *send);

#endif /* _CLOCK_H_ */
