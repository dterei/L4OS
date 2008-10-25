/*
 * IPC Library
 *
 * See header file (<sos/ipc.h>) for explanation of functions and purpose.
 */

#include <stdarg.h>

#include <l4/ipc.h>
#include <l4/message.h>
#include <l4/types.h>

#include <sos/debug.h>
#include <sos/ipc.h>

#define MAGIC_THAT_MAKES_LABELS_WORK 4

static
L4_Msg_t*
ipc_create_msg_v(L4_Word_t label, L4_Msg_t *msg, int count, va_list va)
{
	L4_MsgClear(msg);

	L4_Word_t w;
	for (int i = 0; i < count; i++) {
		w = va_arg(va, L4_Word_t);
		L4_MsgAppendWord(msg, w);
	}

	L4_Set_MsgLabel(msg, label << MAGIC_THAT_MAKES_LABELS_WORK);

	return msg;
}

int
ipc_send_v(L4_ThreadId_t tid, L4_Word_t label, int reply,
		int nRval, L4_Word_t *rvals, int msgLen, va_list va)
{
	L4_MsgTag_t tag;
	L4_Msg_t msg;

	ipc_create_msg_v(label, &msg, msgLen, va);
	L4_MsgLoad(&msg);

	int error = 0;
	switch (reply) {
		case SOS_IPC_CALL:
		case YES_REPLY:
			tag = L4_Call(tid);
			break;

		case SOS_IPC_SEND:
		case NO_REPLY:
			tag = L4_Send(tid);
			break;

		case SOS_IPC_SENDNONBLOCKING:
			tag = L4_Send_Nonblocking(tid);
			break;

		case SOS_IPC_REPLY:
			tag = L4_Reply(tid);
			break;

		default:
			debug_printf("Invalid ipc send type given! (%d)\n", reply);
			error = 1;
	}


	if (L4_IpcFailed(tag) || error == 1) {
		debug_printf("### send_ipc to %ld failed: ", L4_ThreadNo(tid));
		if (error != 1) {
			debug_print_L4Err(L4_ErrorCode());
		} else {
			debug_printf("bad IPC send type\n");
		}
		
		return 1;
	}

	L4_MsgStore(tag, &msg);

	if (reply == YES_REPLY) {
		for (int i = 0; i < nRval; i++) {
			rvals[i] = L4_MsgWord(&msg, i);
		}
	}

	return 0;
}

int
ipc_send(L4_ThreadId_t tid, L4_Word_t label, int reply,
		int nRval, L4_Word_t *rvals, int msgLen, ...)
{
	int r;
	va_list va;

	va_start(va, msgLen);
	r = ipc_send_v(tid, label, reply, nRval, rvals, msgLen, va);
	va_end(va);

	return r;
}

L4_Word_t
ipc_send_simple(L4_ThreadId_t tid, L4_Word_t label, int reply,
		int msgLen, ...)
{
	L4_Word_t rval;
	va_list va;

	va_start(va, msgLen);
	ipc_send_v(tid, label, reply, 1, &rval, msgLen, va);
	va_end(va);

	return rval;
}

L4_Word_t
ipc_send_simple_0(L4_ThreadId_t tid, L4_Word_t label, int reply)
{
	return ipc_send_simple(tid, label, reply, 0);
}

L4_Word_t
ipc_send_simple_1(L4_ThreadId_t tid, L4_Word_t label, int reply,
		L4_Word_t w1)
{
	return ipc_send_simple(tid, label, reply, 1, w1);
}

L4_Word_t
ipc_send_simple_2(L4_ThreadId_t tid, L4_Word_t label, int reply,
		L4_Word_t w1, L4_Word_t w2)
{
	return ipc_send_simple(tid, label, reply, 2, w1, w2);
}

L4_Word_t
ipc_send_simple_3(L4_ThreadId_t tid, L4_Word_t label, int reply,
		L4_Word_t w1, L4_Word_t w2, L4_Word_t w3)
{
	return ipc_send_simple(tid, label, reply, 3, w1, w2, w3);
}

L4_Word_t
ipc_send_simple_4(L4_ThreadId_t tid, L4_Word_t label, int reply,
		L4_Word_t w1, L4_Word_t w2, L4_Word_t w3, L4_Word_t w4)
{
	return ipc_send_simple(tid, label, reply, 4, w1, w2, w3, w4);
}

