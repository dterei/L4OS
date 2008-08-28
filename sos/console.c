#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <l4/thread.h>
#include <l4/types.h>

#include "console.h"
#include "libsos.h"
#include "network.h"

#define verbose 2

// The file names of our consoles
Console_File Console_Files[] = { {"console", 1, CONSOLE_RW_UNLIMITED, 0, 0, {0UL} } };

SpecialFile console_init(SpecialFile sflist) {
	int i;

	dprintf(0, "*** Creating special console files ***\n");

	for (i = 0; i < NUM_CONSOLES; i++) {
		// create new vnode;
		VNode console = (VNode) malloc(sizeof(struct VNode_t));

		// set up console vnode
		console->path = Console_Files[i].path;
		dprintf(0, "*** setting up special file; %s\n", console->path);

		console->stat.st_type = ST_SPECIAL;
		console->stat.st_fmode = FM_READ | FM_WRITE;
		console->stat.st_size = 0;
		console->stat.st_ctime = 0;
		console->stat.st_atime = 0;

		console->open = console_open;
		console->close = console_close;
		console->read = console_read;
		console->write = console_write;

		// setup the console struct
		Console_Files[i].reader_tid = L4_nilthread;
		//Console_Files[i].serial = serial_init();
		console->extra = (void *) (&Console_Files[i]);

		// add console to special files
		SpecialFile sf = (SpecialFile) malloc(sizeof(struct SpecialFile_t));
		sf->file = console;
		sf->next = sflist;
		sflist = sf;
	}

	network_register_serialhandler(serial_read_callback);

	return sflist;
}

// TODO fix synchronisation issues
fildes_t console_open(L4_ThreadId_t tid, VNode self, const char *path,
		fmode_t mode) {

	dprintf(1, "*** console_open(%s, %d)\n", path, mode);

	// make sure they passed in the right vnode
	if (strcmp(self->path, path) != 0) {
		// TODO umm, since this info actually came from SOS (not the
		// user) should probably do something more obvious than
		// returning (-1).
		return (-1);
	}

	// XXX work with multiple writers

	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL)
		return (-1);

	if (cf->readers > cf->Max_Readers) {
		// Reader slots full
		return (-1);
	} else {
		cf->readers++;
		int spaceId = L4_SpaceNo(L4_SenderSpace());
		fildes_t fd = findNextFd(spaceId);

		if (fd < 0) {
			return (-1);
		} else {
			vnodes[spaceId][fd] = self;
			return fd;
		}
	}
}

int console_close(L4_ThreadId_t tid, VNode self, fildes_t file) {
	dprintf(1, "*** console_close: %d\n", file);

	Console_File *cf = (Console_File *) (self->extra);

	// decrease counts
	if (self->stat.st_fmode & FM_WRITE)
		cf->writers--;
	if (self->stat.st_fmode & FM_READ)
		cf->readers--;

	// remove it if waiting on read (although shouldnt be able to occur)
	if (L4_IsThreadEqual(cf->reader_tid, tid))
		cf->reader_tid = L4_nilthread;

	return 0;
}

int console_read(L4_ThreadId_t tid, VNode self, fildes_t file,
		char *buf, size_t nbyte) {

	dprintf(1, "*** console_read: %d %p %d\n", file, buf, nbyte);

	// XXX make sure has permissions, can prbably be moved up to vfs
	if (!(self->stat.st_fmode & FM_READ))
		return (-1);

	Console_File *cf = (Console_File *) (self->extra);

	dprintf(1, "*** console_read: %s, %d, %d, %d, %d ***\n", self->path, cf->Max_Readers, cf->Max_Writers, cf->readers, cf->writers);

	// should check for free slots really in a generic fashion but just simply handle one reader now
	// XXX this won't work, the tid is meaningless.  need a different
	// way to say that there are no readers.
	//if (cf->reader_tid != L4_nilthread)
	//	return (-1);

	// register tid XXX locking?
	// XXX this won't work, the tid is meaningless.  need a different
	// way to say that there are no readers.
	//cf->reader_tid = tid;

	return 0;
}

int console_write(L4_ThreadId_t tid, VNode self, fildes_t file,
		const char *buf, size_t nbyte) {

	dprintf(1, "*** console_write: %d %p %d\n", file, buf, nbyte);

	// XXX make sure has permissions, can prbably be moved up to vfs
	if (!(self->stat.st_fmode & FM_WRITE))
		return (-1);

	char *buf2 = (char *)buf;
	return network_sendstring_char(nbyte, buf2);
}

void serial_read_callback(struct serial *serial, char c) {

	// XXX hack, need proper way of handling finding if we are
	// going be able to ahndle multiple serial devices.
	Console_File *cf = Console_Files + 0;
	(void) cf;

	// XXX this won't work, the tid is meaningless.  need a different
	// way to say that there are no readers.
	//if (cf->reader_tid == L4_nilthread)
	//	return;

	// XXX need to store single char now and send IPC to thread.
	// need to change console struct to actually store read
	// paramaters
}
