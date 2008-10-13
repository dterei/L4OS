#include <elf/elf.h>
#include <sos/sos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "elfload.h"
#include "frames.h"
#include "l4.h"
#include "libsos.h"
#include "list.h"
#include "pager.h"
#include "pair.h"
#include "process.h"
#include "region.h"
#include "syscall.h"

static L4_ThreadId_t elfloadTid; // automatically L4_nilthread

L4_ThreadId_t elfload_get_tid(void) {
	return elfloadTid;
}

static void elfloadHandler(void) {
	L4_Accept(L4_AddAcceptor(L4_UntypedWordsAcceptor, L4_NotifyMsgAcceptor));
	elfloadTid = sos_my_tid();

	printf("elfloadHandler started, tid %ld\n", L4_ThreadNo(elfloadTid));

	L4_ThreadId_t tid;
	L4_Msg_t msg;
	L4_MsgTag_t tag;

	for (;;) {
		tag = L4_Wait(&tid);
		L4_MsgStore(tag, &msg);

		switch (TAG_SYSLAB(tag)) {
			case SOS_PROCESS_CREATE:
				printf("process create\n");
				break;
				
			case SOS_REPLY:
				printf("l4 reply\n");
		}
	}
}

void elfload_init(void) {
	Process *elfload = process_init(1);

	process_set_name(elfload, "elfload");
	process_prepare(elfload);
	process_set_ip(elfload, (void*) elfloadHandler);
	process_set_sp(elfload, (char*) frame_alloc() + PAGESIZE - sizeof(L4_Word_t));

	process_run(elfload);

	while (L4_IsThreadEqual(elfloadTid, L4_nilthread)) L4_Yield();
}

