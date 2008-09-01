#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "l4.h"
#include "libsos.h"
#include "network.h"

#define verbose 1

// The file names of our consoles
Console_File Console_Files[] = { {"console", 1, CONSOLE_RW_UNLIMITED, 0, 0, {0UL} } };

SpecialFile console_init(SpecialFile sflist) {
	int i;

	dprintf(1, "*** console_init: creating special console files ***\n");

	for (i = 0; i < NUM_CONSOLES; i++) {
		// create new vnode;
		VNode console = (VNode) malloc(sizeof(struct VNode_t));

		// set up console vnode
		console->path = Console_Files[i].path;
		dprintf(1, "*** console_init: setting up special file; %s\n", console->path);

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
		Console_Files[i].reading_tid = L4_nilthread;
		console->extra = (void *) (&Console_Files[i]);

		// add console to special files
		SpecialFile sf = (SpecialFile) malloc(sizeof(struct SpecialFile_t));
		sf->file = console;
		sf->next = sflist;
		sflist = sf;
	}

	int r = network_register_serialhandler(serial_read_callback);
	dprintf(1, "*** console_init: register = %d\n", r);

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

	// TODO work with multiple writers
	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL)
		return (-1);

	fildes_t fd = -1;
	if (mode & FM_READ) {
		if (cf->readers > cf->Max_Readers) {
			// Reader slots full
			return (-1);
		} else {
			cf->readers++;
			int spaceId = L4_SpaceNo(L4_SenderSpace());
			fd = findNextFd(spaceId);

			if (fd < 0) {
				return (-1);
			} else {
				vnodes[spaceId][fd] = self;
			}
		}
	}

	if (mode & FM_WRITE) {
		if (cf->writers > cf->Max_Writers) {
			// Writers slots full
			return (-1);
		} else {
			cf->writers++;
			if (fd == -1) {
				int spaceId = L4_SpaceNo(L4_SenderSpace());
				fd = findNextFd(spaceId);

				if (fd < 0) {
					return (-1);
				} else {
					vnodes[spaceId][fd] = self;
				}
			}
		}
	}

	return fd;
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
	if (L4_IsThreadEqual(cf->reading_tid, tid))
		cf->reading_tid = L4_nilthread;

	return 0;
}

void console_read(L4_ThreadId_t tid, VNode self, fildes_t file,
		char *buf, size_t nbyte, int *rval) {

	dprintf(1, "*** console_read: %d, %d %p %d from %p\n", file, buf, nbyte, tid.raw);

	// XXX make sure has permissions, can prbably be moved up to vfs
	if (!(self->stat.st_fmode & FM_READ)) {
		*rval = (-1);
		return;
	}

	// make sure console exists
	if (self == NULL || self->extra == NULL) {
		*rval = (-1);
		return;
	}

	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL) {
		*rval = (-1);
		return;
	}

	// XXX for some reason this causes a page fault
	// should check for free slots really in a generic fashion but just simply handle one reader now
	//if (L4_IsNilThread(cf->reading_tid))
		//return (-1);

	// store read request
	cf->reading_tid = tid;
	cf->reading_buf = buf;
	cf->reading_nbyte = nbyte;
	cf->reading_rval = rval;
	*rval = 0;
}

void console_write(L4_ThreadId_t tid, VNode self, fildes_t file,
		const char *buf, size_t nbyte, int *rval) {

	dprintf(1, "*** console_write: %d %p %d\n", file, buf, nbyte);

	// XXX make sure has permissions, can prbably be moved up to vfs
	if (!(self->stat.st_fmode & FM_WRITE)) {
		*rval = (-1);
		return;
	}

	// because it doesn't like a const
	char *buf2 = (char *)buf;
	*rval = network_sendstring_char(nbyte, buf2);
}

void serial_read_callback(struct serial *serial, char c) {
	dprintf(1, "*** serial_read_callback: %c\n", c);

	// TODO hack, need proper way of handling finding if we are
	// going be able to handle multiple serial devices.
	Console_File *cf = &Console_Files[0];
	int read = 0;

	if (L4_IsNilThread(cf->reading_tid))
		return;

	dprintf(1, "*** serial_read_callback: %c, send to %p\n", c, (cf->reading_tid).raw);

	if (cf->reading_nbyte > cf->reading_rbytes + 1) {
		cf->reading_buf[cf->reading_rbytes] = c;
		cf->reading_buf[cf->reading_rbytes + 1] = '\0';
		cf->reading_rbytes++;
	} else {
		// TODO else... what?
	}

	read = cf->reading_rbytes;
	
	if (c == '\n' || cf->reading_rbytes >= cf->reading_nbyte) {
		*(cf->reading_rval) = cf->reading_rbytes;

		dprintf(1, "*** serial_read_callback: send tid = %d, buf = %s\n",
				L4_ThreadNo(cf->reading_tid), cf->reading_buf);

		/*
		for (int i = 0; i < cf->reading_rbytes; i++) {
			L4_MsgTag_t calltag = L4_Reply(cf->reading_tid);
			if(L4_IpcFailed(calltag)) {
				dprintf(0, "*** L4 hates you %d %d ***\n", i, L4_ErrorCode());
			}
		}
		*/

		// XXX this is a complete freaking mystery.  Originally we
		// thought that the need to call L4_Reply twice was because
		// we were using UncachedMemory for everything - and that
		// the first call failed but did something magic with the
		// cache to make it work the second time - but now that this
		// has been fixed it STILL doesn't work.
		L4_CacheFlushAll(); // TODO only flush buffer
		L4_MsgTag_t tag = L4_Reply(cf->reading_tid);
		if (L4_IpcFailed(tag)) {
			dprintf(0, "!!! serial_read_callback: reply failed! Code=%d\n",
					L4_ErrorCode());
			L4_Reply(cf->reading_tid);
		}

		cf->reading_rbytes = 0;
	}

	dprintf(1, "*** serial read: buf = %s\n", cf->reading_buf);
}

