/****************************************************************************
 *
 *      $Id:  $
 *
 *      Description: Simple milestone 0 code.
 *      		     Libc will need sos_write & sos_read implemented.
 *
 *      Author:      Ben Leslie
 *
 ****************************************************************************/

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <sos/sos.h>
#include <sos/ttyout.h>

#include <l4/types.h>
#include <l4/kdebug.h>
#include <l4/ipc.h>

void
ttyout_init(void) {
}

size_t
sos_write(const void *vData, long int position, size_t count, void *handle) {
	return write(stdout_fd, vData, count);
}

size_t
sos_read(void *vData, long int position, size_t count, void *handle) {
	assert(!"sos_read called, open console read instead!");
	(void) position;
	(void) handle;
	return read(stdin_fd, vData, count);
}

void abort(void) {
	L4_KDB_Enter("sos abort()ed");
	while (1);
}

void _Exit(int status) {
	abort();
}

