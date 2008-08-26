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

#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>

#include <sos/sos.h>
#include <sos/ttyout.h>

#include <l4/types.h>
#include <l4/kdebug.h>
#include <l4/ipc.h>

#define MAX_NETPRINT 31

void
ttyout_init(void) {
}

size_t
sos_write(const void *vData, long int position, size_t count, void *handle)
{
	L4_Msg_t msg;
	int i, j, k;
	const char *realdata = vData;

	for (i = 0; i < count; i += k) {
		L4_MsgClear(&msg);
		L4_Set_MsgLabel(&msg, SOS_NETPRINT);

		k = MAX_NETPRINT;
		if (k > count - i) {
			k = count - i;
		}

		for (j = i; j < i + k; j++) {
			L4_MsgAppendWord(&msg, realdata[j]);
		}

		L4_MsgLoad(&msg);
		L4_Send(L4_rootserver);
	}

	return count;
}

size_t
sos_read(void *vData, long int position, size_t count, void *handle)
{
	size_t i;
	okl4_kdb_res_t res;
	char *realdata = vData;
	for (i = 0; i < count; i++) // Fix this to use your syscall
		res = L4_KDB_ReadChar_Blocked(&(realdata[i]));
	return count;
}

void
abort(void)
{
	L4_KDB_Enter("sos abort()ed");
	while(1); /* We don't return after this */
}

void _Exit(int status) { abort(); }

