#include <stdarg.h>

#include <clock/clock.h>
#include <sos/sos.h>

#include "constants.h"
#include "frames.h"
#include "l4.h"
#include "libsos.h"
#include "network.h"
#include "pager.h"
#include "process.h"
#include "syscall.h"
#include "vfs.h"

#define verbose 1

static int rval;

void
syscall_reply(L4_ThreadId_t tid, L4_Word_t xval)
{
	syscall_reply_m(tid, 1, xval);
}

void
syscall_reply_m(L4_ThreadId_t tid, int count, ...)
{
	assert(!L4_IsThreadEqual(tid, L4_rootserver));
	L4_MsgTag_t tag;

	L4_CacheFlushAll();

	L4_Msg_t msg;
	L4_MsgClear(&msg);
	L4_Set_MsgLabel(&msg, SOS_REPLY << 4);

	va_list va;
	va_start(va, count);
	L4_Word_t w;
	for (int i = 0; i < count; i++) {
		w = va_arg(va, L4_Word_t);
		L4_MsgAppendWord(&msg, w);
	}
	va_end(va);

	L4_MsgLoad(&msg);

	int send = L4_IsThreadEqual(tid, virtual_pager);

	if (send) {
		// this isn't a very nice way of doing it, but all calls to the pager
		// need to be sent (since they are nonblocking).  ideally this will
		// be set up by the caller not as a hack to syscall_reply.
		dprintf(2, "*** syscall_reply: send to %ld\n", L4_ThreadNo(tid));
		tag = L4_Send(tid);
	} else {
		dprintf(2, "*** syscall_reply: reply to %ld\n", L4_ThreadNo(tid));
		tag = L4_Reply(tid);
	}

	if (L4_IpcFailed(tag)) {
		dprintf(1, "!!! syscall_reply (%s) to %ld failed: ",
				send ? "send" : "reply", L4_ThreadNo(tid));
		sos_print_error(L4_ErrorCode());
	}
}

static L4_Word_t *buffer(L4_ThreadId_t tid) {
	return (L4_Word_t*) pager_buffer(tid);
}

static char *wordAlign(char *s) {
	unsigned int x = (unsigned int) s;
	x--;
	x += sizeof(L4_Word_t) - (x % sizeof(L4_Word_t));
	return (char*) x;
}

int
syscall_handle(L4_MsgTag_t tag, L4_ThreadId_t tid, L4_Msg_t *msg)
{
	char *buf; (void) buf;
	L4_Word_t word;

	L4_CacheFlushAll();

	dprintf(2, "*** syscall_handle: got %s\n", syscall_show(TAG_SYSLAB(tag)));

	switch(TAG_SYSLAB(tag)) {
		case SOS_KERNEL_PRINT:
			pager_buffer(tid)[MAX_IO_BUF - 1] = '\0';
			printf("%s", pager_buffer(tid));
			break;

		case SOS_DEBUG_FLUSH:
			pager_flush(tid, msg);
			break;

		case SOS_MOREMEM:
			syscall_reply(tid,
					sos_moremem((uintptr_t*) buffer(tid), L4_MsgWord(msg, 0)));
			break;

		case SOS_OPEN:
			vfs_open(tid, pager_buffer(tid), (fmode_t) L4_MsgWord(msg, 0));
			break;

		case SOS_CLOSE:
			vfs_close(tid, (fildes_t) L4_MsgWord(msg, 0), &rval);
			break;

		case SOS_READ:
			vfs_read(tid,
					(fildes_t) L4_MsgWord(msg, 0),
					pager_buffer(tid),
					(size_t) L4_MsgWord(msg, 1),
					&rval);
			break;

		case SOS_WRITE:
			vfs_write(tid,
					(fildes_t) L4_MsgWord(msg, 0),
					pager_buffer(tid),
					(size_t) L4_MsgWord(msg, 1),
					&rval);
			break;

		case SOS_LSEEK:
			vfs_lseek(tid,
					(fildes_t) L4_MsgWord(msg, 0),
					(fpos_t) L4_MsgWord(msg, 1),
					(int) L4_MsgWord(msg, 2),
					&rval);
			break;

		case SOS_GETDIRENT:
			vfs_getdirent(tid,
					(int) L4_MsgWord(msg, 0),
					pager_buffer(tid),
					(size_t) L4_MsgWord(msg, 1),
					&rval);
			break;

		case SOS_STAT:
			buf = pager_buffer(tid);
			vfs_stat(tid, buf, (stat_t*) wordAlign(buf + strlen(buf) + 1), &rval);
			break;

		case SOS_REMOVE:
			vfs_remove(tid, pager_buffer(tid), &rval);
			break;

		case SOS_TIME_STAMP:
			rval = (L4_Word_t) time_stamp();
			word = (L4_Word_t) (time_stamp() >> 32);
			syscall_reply_m(tid, 2, rval, word);
			break;

		case SOS_USLEEP:
			register_timer((uint64_t) L4_MsgWord(msg, 0), tid);
			break;

		case SOS_PROCESS_DELETE:
			rval = process_kill(process_lookup(L4_MsgWord(msg, 0)));
			syscall_reply(tid, rval);
			break;

		case SOS_MY_ID:
			rval = process_get_pid(process_lookup(L4_ThreadNo(tid)));
			syscall_reply(tid, rval);
			break;

		case SOS_PROCESS_WAIT:
			word = L4_MsgWord(msg, 0);
			if (word == ((L4_Word_t) -1)) {
				process_wait_any(process_lookup(L4_ThreadNo(tid)));
			} else {
				process_wait_for(process_lookup(word),
						process_lookup(L4_ThreadNo(tid)));
			}
			break;

		case SOS_PROCESS_STATUS:
			rval = process_write_status((process_t*) buffer(tid), L4_MsgWord(msg, 0));
			syscall_reply(tid, rval);
			break;

		case SOS_MEMUSE:
			rval = sos_memuse();
			syscall_reply(tid, rval);
			break;

		case SOS_VPAGER:
			rval = L4_ThreadNo(virtual_pager);
			syscall_reply(tid, rval);
			break;

		case SOS_PROCESS_CREATE:

		default:
			// Unknown system call, so we don't want to reply to this thread
			dprintf(0, "!!! unrecognised syscall id=%d\n", TAG_SYSLAB(tag));
			sos_print_l4memory(msg, L4_UntypedWords(tag) * sizeof(uint32_t));
			break;
	}

	return 0;
}

