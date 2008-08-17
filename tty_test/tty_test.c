/****************************************************************************
 *
 *      $Id:  $
 *
 *      Description: Simple milestone 0 test.
 *
 *      Author:			Godfrey van der Linden
 *      Original Author:	Ben Leslie
 *
 ****************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <l4/types.h>

#include <l4/ipc.h>
#include <l4/message.h>
#include <l4/thread.h>

#include "ttyout.h"

// Block a thread forever
static void
thread_block(void)
{
    L4_Msg_t msg;

    L4_MsgClear(&msg);
    L4_MsgTag_t tag = L4_Receive(L4_Myself());

    if (L4_IpcFailed(tag)) {
	printf("blocking thread failed: %lx\n", tag.raw);
	*(char *) 0 = 0;
    }
}

int main(void)
{
	/* initialise communication */
	ttyout_init();

	do {
		printf("task:\tHello world, I'm\ttty_test!\n");
		//for (int i = ' '; i < '~' - 1; i++) {
		//	printf("testing characters %c and %c\n", (char) i, (char) (i + 1));
		//}
		thread_block();
		// sleep(1);	// Implement this as a syscall
	} while(1);

	return 0;
}
