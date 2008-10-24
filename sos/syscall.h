/*
 * Handle incoming syscalls from user space.
 *
 * Also defines some private IPC message types which can only be
 * called by threads running in SOS's address space.
 */
#ifndef _syscall_h
#define _syscall_h

#include <sos/sos.h>

#include "l4.h"

/* Struct of extra syscalls available to root threads, can't be used by userspace */
typedef enum {
	// Open syscall that can be used by the pager to emulate an SOS_OPEN call from a specified process
	PSOS_OPEN = SOS_NULL + 1,
	PSOS_DUP,
	PSOS_FLUSH,
	PSOS_CLOSE,
} psyscall_t;

void syscall_reply(L4_ThreadId_t tid, L4_Word_t rval);
void syscall_reply_v(L4_ThreadId_t tid, int count, ...);

int syscall_handle(L4_MsgTag_t tag, L4_ThreadId_t tid, L4_Msg_t *msg);

#endif // sos/syscall.h
