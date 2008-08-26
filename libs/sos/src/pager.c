#include <l4/ipc.h>
#include <l4/message.h>

#include <sos/sos.h>
#include <sos/pager.h>

void
sos_debug_flush(void)
{
	L4_Msg_t msg;
	L4_MsgClear(&msg);
	L4_Set_MsgLabel(&msg, SOS_DEBUG_FLUSH);
	L4_MsgLoad(&msg);
	L4_Send(L4_rootserver);
}

