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

#define verbose 3

static L4_ThreadId_t elfloadTid; // automatically L4_nilthread

L4_ThreadId_t elfload_get_tid(void) {
	return elfloadTid;
}

static fildes_t openPhys(char *path, fmode_t mode) {
	L4_Msg_t msg;
	syscall_prepare(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) mode);

	memcpy(pager_buffer(sos_my_tid()), path, MAX_IO_BUF);
	return syscall(L4_rootserver, SOS_OPEN, YES_REPLY, &msg);
}

static int readPhys(fildes_t file, char *buf, size_t nbyte) {
	int rval;
	L4_Msg_t msg;
	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);

	rval = syscall(L4_rootserver, SOS_READ, YES_REPLY, &msg);
	memcpy(buf, pager_buffer(sos_my_tid()), MAX_IO_BUF);

	return rval;
}

static void processCreate(L4_ThreadId_t tid, char *path) {
	Process *p;
	fildes_t elfFile;
	struct Elf32_Header elfHeader;
	struct Elf32_Phdr elfPhdr;
	int rval;

	// Open the elf file
	elfFile = openPhys(path, FM_READ);

	if (elfFile < 0) {
		// Couldn't open the file
		dprintf(1, "!!! processCreate: couldn't open %s\n", path);
		syscall_reply(tid, (-1));
		return;
	} else {
		dprintf(2, "*** processCreate: opened with fd=%d\n", elfFile);
	}

	// Read the header data
	rval = readPhys(elfFile, (char*) &elfHeader, sizeof(struct Elf32_Header));

	if (rval != sizeof(struct Elf32_Header)) {
		// Something bad happened
		dprintf(1, "!!! processCreate: couldn't reader ELF header\n");
		syscall_reply(tid, (-1));
		return;
	} else if (elf_checkFile(&elfHeader) != 0) {
		// It wasn't an ELF file
		dprintf(1, "!!! processCreate: not an ELF file\n");
		syscall_reply(tid, (-1));
		return;
	}

	// Read in all the program sections
	for (int i = 0; i < elf_getNumProgramHeaders(&elfHeader); i++) {
		dprintf(2, "Program section %d\n", i);
	}

	(void) p;
	(void) elfPhdr;

	printf("processCreate: not fully implemented\n");
	syscall_reply(tid, (-1));
}

static void elfloadHandler(void) {
	L4_Accept(L4_AddAcceptor(L4_UntypedWordsAcceptor, L4_NotifyMsgAcceptor));
	elfloadTid = sos_my_tid();

	dprintf(2, "*** elfloadHandler: tid %ld\n", L4_ThreadNo(elfloadTid));

	L4_ThreadId_t tid;
	L4_Msg_t msg;
	L4_MsgTag_t tag;

	for (;;) {
		tag = L4_Wait(&tid);
		L4_MsgStore(tag, &msg);
		tid = sos_cap2tid(tid);

		dprintf(2, "*** elfloadHandler: from %ld\n", L4_ThreadNo(tid));

		switch (TAG_SYSLAB(tag)) {
			case SOS_PROCESS_CREATE:
				processCreate(tid, (char*) pager_buffer(tid));
				break;
				
			case SOS_REPLY:
				printf("l4 reply\n");
		}

		dprintf(2, "*** elfloadHandler: dealt with %ld\n", L4_ThreadNo(tid));
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
	dprintf(2, "*** elfload_init: started elfload thread\n");
}

