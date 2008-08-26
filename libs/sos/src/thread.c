#include <stdio.h>

#include <l4/ipc.h>
#include <l4/message.h>

#include <sos/thread.h>

void
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
