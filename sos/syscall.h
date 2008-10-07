#ifndef _syscall_h
#define _syscall_h

#include "l4.h"

void syscall_reply(L4_ThreadId_t tid, L4_Word_t rval);
void syscall_reply_m(L4_ThreadId_t tid, int count, ...);

int syscall_handle(L4_MsgTag_t tag, L4_ThreadId_t tid, L4_Msg_t *msg);

#endif // sos/syscall.h
