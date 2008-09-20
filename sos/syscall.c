#include <clock/clock.h>
#include <sos/sos.h>

#include "syscall.h"

#include "l4.h"
#include "libsos.h"
#include "network.h"

#include "pager.h"
#include "frames.h"
#include "vfs.h"

#define verbose 1

void
syscall_reply(L4_ThreadId_t tid, L4_Word_t rval)
{
	L4_CacheFlushAll();

	msgClearWith(rval);
	L4_Reply(tid);
}

int
syscall_handle(L4_MsgTag_t tag, L4_ThreadId_t tid, L4_Msg_t *msg)
{
	L4_CacheFlushAll();
	L4_Word_t rval;

	switch(TAG_SYSLAB(tag)) {
		case SOS_KERNEL_PRINT:
			pager_buffer(tid)[MAX_IO_BUF - 1] = '\0';
			printf("%s", pager_buffer(tid));
			break;

		case SOS_DEBUG_FLUSH:
			pager_flush(tid, msg);
			break;

		case SOS_MOREMEM:
			rval = (L4_Word_t) sos_moremem(
					(uintptr_t*) sender2kernel(L4_MsgWord(msg, 0)),
					(unsigned int) L4_MsgWord(msg, 1));
			syscall_reply(tid, rval);
			break;

		case SOS_COPYIN:
			copyIn(tid, (void*) L4_MsgWord(msg, 0), (size_t) L4_MsgWord(msg, 1));
			break;

		case SOS_COPYOUT:
			copyOut(tid, (void*) L4_MsgWord(msg, 0), (size_t) L4_MsgWord(msg, 1));
			break;

		case SOS_OPEN:
			vfs_open(tid,
					(char*) sender2kernel(L4_MsgWord(msg, 0)),
					(fmode_t) L4_MsgWord(msg, 1),
					(int*) sender2kernel(L4_MsgWord(msg, 2)));
			break;

		case SOS_CLOSE:
			vfs_close(tid,
					(fildes_t) L4_MsgWord(msg, 0),
					(int*) sender2kernel(L4_MsgWord(msg, 1)));
			break;

		case SOS_READ:
			vfs_read(tid,
					(fildes_t) L4_MsgWord(msg, 0),
					(char*) sender2kernel(L4_MsgWord(msg, 1)),
					(size_t) L4_MsgWord(msg, 2),
					(int*) sender2kernel(L4_MsgWord(msg, 3)));
			break;

		case SOS_WRITE:
			vfs_write(tid,
					(fildes_t) L4_MsgWord(msg, 0),
					(char*) sender2kernel(L4_MsgWord(msg, 1)),
					(size_t) L4_MsgWord(msg, 2),
					(int*) sender2kernel(L4_MsgWord(msg, 3)));
			break;

		case SOS_GETDIRENT:
			vfs_getdirent(tid,
					(int) L4_MsgWord(msg, 0),
					(char*) sender2kernel(L4_MsgWord(msg, 1)),
					(size_t) L4_MsgWord(msg, 2),
					(int*) sender2kernel(L4_MsgWord(msg, 3)));
			break;

		case SOS_STAT:
			vfs_stat(tid,
					(char*) sender2kernel(L4_MsgWord(msg, 0)),
					(stat_t*) L4_MsgWord(msg, 1),
					(int*) sender2kernel(L4_MsgWord(msg, 2)));
			break;

		case SOS_TIME_STAMP:
			syscall_reply(tid, (L4_Word_t) time_stamp());
			break;

		case SOS_SLEEP:
			register_timer((uint64_t) L4_MsgWord(msg, 0) * 1000, tid);
			break;

		case SOS_PROCESS_CREATE:
		case SOS_PROCESS_DELETE:
		case SOS_MY_ID:
		case SOS_PROCESS_STATUS:
		case SOS_PROCESS_WAIT:
		case SOS_SHARE_VM:
		default:
			// Unknown system call, so we don't want to reply to this thread
			dprintf(0, "!!! unrecognised syscall id=%d\n", TAG_SYSLAB(tag));
			sos_print_l4memory(msg, L4_UntypedWords(tag) * sizeof(uint32_t));
			break;
	}

	return 0;
}

