#include <sos/sos.h>

#include "syscall.h"

#include "l4.h"
#include "libsos.h"
#include "network.h"

#include "pager.h"
#include "frames.h"
#include "vfs.h"

#define verbose 0

void
syscall_reply(L4_ThreadId_t tid)
{
	L4_CacheFlushAll();

	msgClear();
	L4_Reply(tid);
}

int
syscall_handle(L4_MsgTag_t tag, L4_ThreadId_t tid, L4_Msg_t *msg)
{
	L4_CacheFlushAll();
	L4_Word_t rval;
	int send = 1;

	switch(TAG_SYSLAB(tag))
	{
		case SOS_NETPRINT:
			network_sendstring_int(msg->tag.X.u, (int*) (msg->msg + 1));
			send = 0;
			break;

		case SOS_DEBUG_FLUSH:
			pager_flush(tid, msg);	
			send = 0;
			break;

		case SOS_MOREMEM:
			rval = (L4_Word_t) sos_moremem(
					(uintptr_t*) sender2kernel(L4_MsgWord(msg, 0)),
					(unsigned int) L4_MsgWord(msg, 1));
			*(sender2kernel(L4_MsgWord(msg, 2))) = rval;
			break;

		case SOS_STARTME:
			break;

			// XXX must check that sender2kernel doesn't return null,
			// or the user can crash the kernel!!!
			// XXX check the rights of the page before doing anything
			// in sender2kernel - CAN'T just map with the userspace
			// access rights since then userspace could crash the kernel,
			// and can't just map with all access rights because then
			// user programs could access memory they're not allowed to.
			// XXX what if contiguous virtual pages arent contiguous
			// physically... then like addr[HUUUGE] will actually
			// index in to a completely different physical frame.

		case SOS_OPEN:
			vfs_open(tid,
					(char*) sender2kernel(L4_MsgWord(msg, 0)),
					(fmode_t) L4_MsgWord(msg, 1),
					(int*) sender2kernel(L4_MsgWord(msg, 2)));
			send = 0;
			break;

		case SOS_CLOSE:
			vfs_close(tid,
					(fildes_t) L4_MsgWord(msg, 0),
					(int*) sender2kernel(L4_MsgWord(msg, 1)));
			send = 0;
			break;

		case SOS_READ:
			vfs_read(tid,
					(fildes_t) L4_MsgWord(msg, 0),
					(char*) sender2kernel(L4_MsgWord(msg, 1)),
					(size_t) L4_MsgWord(msg, 2),
					(int*) sender2kernel(L4_MsgWord(msg, 3)));
			send = 0;
			break;

		case SOS_WRITE:
			vfs_write(tid,
					(fildes_t) L4_MsgWord(msg, 0),
					(char*) sender2kernel(L4_MsgWord(msg, 1)),
					(size_t) L4_MsgWord(msg, 2),
					(int*) sender2kernel(L4_MsgWord(msg, 3)));
			send = 0;
			break;

		case SOS_GETDIRENT:
			vfs_getdirent(tid,
					(int) L4_MsgWord(msg, 0),
					(char*) sender2kernel(L4_MsgWord(msg, 1)),
					(size_t) L4_MsgWord(msg, 2),
					(int*) sender2kernel(L4_MsgWord(msg, 3)));
			send = 0;
			break;

		case SOS_STAT:
			vfs_stat(tid,
					(char*) sender2kernel(L4_MsgWord(msg, 0)),
					(stat_t*) L4_MsgWord(msg, 1),
					(int*) sender2kernel(L4_MsgWord(msg, 2)));
			send = 0;
			break;

		case SOS_PROCESS_CREATE:
		case SOS_PROCESS_DELETE:
		case SOS_MY_ID:
		case SOS_PROCESS_STATUS:
		case SOS_PROCESS_WAIT:
		case SOS_TIME_STAMP:
		case SOS_SLEEP:
		case SOS_SHARE_VM:
		default:
			// Unknown system call, so we don't want to reply to this thread
			dprintf(0, "!!! unrecognised syscall id=%d\n", TAG_SYSLAB(tag));
			sos_print_l4memory(msg, L4_UntypedWords(tag) * sizeof(uint32_t));
			send = 0;
			break;
	}

	return send;
}

