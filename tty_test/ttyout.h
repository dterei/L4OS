#ifndef _TTYOUT_H
#define _TTYOUT_H

#include <stdio.h>

/* Initialise tty output.
 * Must be called prior to any IPC receive operation.
 * Returns task ID of initial server (OS).
 */
extern void ttyout_init(void);

/* routines needed by the libs/c i.e. -lc implementation */

/* Print to the proper console.  You will need to finish these implementations
 */
extern size_t
sos_write(const void *data, long int position, size_t count, void *handle);
extern size_t
sos_read(void *data, long int position, size_t count, void *handle);


#endif
