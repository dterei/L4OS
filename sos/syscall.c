#include <stdarg.h>

#include <clock/clock.h>
#include <sos/sos.h>

#include "cache.h"
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

void
syscall_reply(L4_ThreadId_t tid, L4_Word_t rval)
{
	syscall_reply_m(tid, 1, rval);
}

void
syscall_reply_m(L4_ThreadId_t tid, int count, ...)
{
	// make sure process/thread still exists
	Process *p = process_lookup(L4_ThreadNo(tid));
	if (p == NULL) {
		dprintf(0, "!!! Process doesn't seem to exist anymore! (p %d)\n", process_get_pid(p));
		return;
	}

	// ignore if a reponse to the roottask, probably a faked syscall
	if (L4_IsThreadEqual(tid, L4_rootserver)) {
		dprintf(0, "!!! syscall_reply_m: ignoring reply to roottask\n");
		return;
	}

	L4_MsgTag_t tag;

	CACHE_FLUSH_ALL();

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

	if (L4_IsThreadEqual(tid, pager_get_tid())) {
		// TODO Do this nicely (either set up by the caller, or generically)
		tag = L4_Send(tid);
	} else {
		tag = L4_Reply(tid);
	}

	if (L4_IpcFailed(tag)) {
		dprintf(1, "!!! syscall_reply to %ld failed: ", L4_ThreadNo(tid));
		sos_print_error(L4_ErrorCode());
	} else {
		dprintf(2, "*** syscall_reply to %ld success\n", L4_ThreadNo(tid));
	}
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
	char *buf;

	if (!L4_IsSpaceEqual(L4_SenderSpace(), L4_rootspace)) {
		dprintf(1, "*** syscall_handle: got tid=%ld tag=%s\n",
				L4_ThreadNo(tid), syscall_show(TAG_SYSLAB(tag)));
	}

	switch(TAG_SYSLAB(tag)) {
		case SOS_KERNEL_PRINT:
			pager_buffer(tid)[MAX_IO_BUF - 1] = '\0';
			printf("%s", pager_buffer(tid));
			break;

		case SOS_OPEN:
			vfs_open(tid, pager_buffer(tid),
					(fmode_t) L4_MsgWord(msg, 0),
					(unsigned int) L4_MsgWord(msg, 1),
					(unsigned int) L4_MsgWord(msg, 2));
			break;

		case SOS_CLOSE:
			vfs_close(tid, (fildes_t) L4_MsgWord(msg, 0));
			break;

		case SOS_READ:
			vfs_read(tid,
					(fildes_t) L4_MsgWord(msg, 0),
					pager_buffer(tid),
					(size_t) L4_MsgWord(msg, 1));
			break;

		case SOS_WRITE:
			vfs_write(tid,
					(fildes_t) L4_MsgWord(msg, 0),
					pager_buffer(tid),
					(size_t) L4_MsgWord(msg, 1));
			break;

		case SOS_FLUSH:
			vfs_flush(tid,
					(fildes_t) L4_MsgWord(msg, 0));
			break;

		case SOS_LSEEK:
			vfs_lseek(tid,
					(fildes_t) L4_MsgWord(msg, 0),
					(fpos_t) L4_MsgWord(msg, 1),
					(int) L4_MsgWord(msg, 2));
			break;

		case SOS_GETDIRENT:
			vfs_getdirent(tid,
					(int) L4_MsgWord(msg, 0),
					pager_buffer(tid),
					(size_t) L4_MsgWord(msg, 1));
			break;

		case SOS_STAT:
			buf = pager_buffer(tid);
			vfs_stat(tid, buf, (stat_t*) wordAlign(buf + strlen(buf) + 1));
			break;

		case SOS_REMOVE:
			vfs_remove(tid, pager_buffer(tid));
			break;

		case SOS_TIME_STAMP:
			syscall_reply_m(tid, 2,
					(L4_Word_t) time_stamp(),
					(L4_Word_t) (time_stamp() >> 32));
			break;

		case SOS_USLEEP:
			register_timer((uint64_t) L4_MsgWord(msg, 0), tid);
			break;

		case SOS_MY_ID:
			syscall_reply(tid, process_get_pid(process_lookup(L4_ThreadNo(tid))));
			break;

		case SOS_VPAGER:
			syscall_reply(tid, L4_ThreadNo(pager_get_tid()));
			break;

		default:
			dprintf(0, "!!! rootserver: unhandled syscall tid=%ld id=%d name=%s\n",
					L4_ThreadNo(tid), TAG_SYSLAB(tag), syscall_show(TAG_SYSLAB(tag)));
			sos_print_l4memory(msg, L4_UntypedWords(tag) * sizeof(uint32_t));
			break;
	}

	return 0;
}

