#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "l4.h"
#include "libsos.h"
#include "network.h"

#define verbose 3

// The file names of our consoles
Console_File Console_Files[] = { {"console", 1, CONSOLE_RW_UNLIMITED, 0, 0} };

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
		Console_Files[i].reader.tid = L4_nilthread;
		console->extra = (void *) (&Console_Files[i]);

		// add console to special files
		SpecialFile sf = (SpecialFile) malloc(sizeof(struct SpecialFile_t));
		sf->file = console;
		sf->next = sflist;
		sflist = sf;
	}

	// register callback
	int r = network_register_serialhandler(serial_read_callback);
	dprintf(1, "*** console_init: register = %d\n", r);

	return sflist;
}

fildes_t console_open(L4_ThreadId_t tid, VNode self, const char *path,
		fmode_t mode) {

	dprintf(1, "*** console_open(%s, %d)\n", path, mode);

	// make sure console exists
	if (self == NULL) {
		return (-1);
	}

	// make sure they passed in the right vnode
	if (strcmp(self->path, path) != 0) {
		return (-1);
	}

	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL) {
		return (-1);
	}

	fildes_t fd = -1;

	// open file for reading
	if (mode & FM_READ) {
		// check if reader slots full
		if (cf->readers > cf->Max_Readers) {
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

	// open file for writing
	if (mode & FM_WRITE) {
		// check if writers slots full
		if (cf->writers > cf->Max_Writers) {
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

	// make sure console exists
	if (self == NULL) {
		return (-1);
	}

	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL) {
		return (-1);
	}

	// decrease counts
	if (self->stat.st_fmode & FM_WRITE) {
		cf->writers--;
	}
	if (self->stat.st_fmode & FM_READ) {
		cf->readers--;
	}

	// remove it if waiting on read (although shouldnt be able to occur)
	if (L4_IsThreadEqual(cf->reader.tid, tid)) {
		cf->reader.tid = L4_nilthread;
		cf->reader.buf = NULL;
		cf->reader.rval = NULL;
		cf->reader.nbyte = 0;
		cf->reader.rbyte = 0;
	}

	return 0;
}

void console_read(L4_ThreadId_t tid, VNode self, fildes_t file,
		char *buf, size_t nbyte, int *rval) {

	dprintf(1, "*** console_read: %d, %p, %d from %p\n", file, buf, nbyte, tid.raw);

	// make sure console exists
	if (self == NULL) {
		*rval = (-1);
		return;
	}

	// XXX make sure has permissions, can prbably be moved up to vfs
	if (!(self->stat.st_fmode & FM_READ)) {
		*rval = (-1);
		return;
	}

	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL) {
		*rval = (-1);
		return;
	}

	// XXX for some reason this causes a page fault
	//if (L4_IsNilThread(cf->reading_tid))
		//return (-1);

	// store read request
	cf->reader.tid = tid;
	cf->reader.buf = buf;
	cf->reader.rval = rval;
	cf->reader.nbyte = nbyte;
	cf->reader.rbyte = 0;
	*rval = 0;

	L4_CacheFlushAll();
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
	// XXX Need to make sure we don't block up sos too long.
	// either use a thread just for writes or continuations.
	char *buf2 = (char *)buf;
	*rval = network_sendstring_char(nbyte, buf2);
}

void serial_read_callback(struct serial *serial, char c) {
	dprintf(1, "*** serial_read_callback: %c\n", c);

	// TODO hack, need proper way of handling finding if we are
	// going be able to handle multiple serial devices.
	Console_File *cf = &Console_Files[0];
	if (cf == NULL || L4_IsNilThread(cf->reader.tid)) {
		return;
	}

	dprintf(2, "*** serial_read_callback: %c, send to %p\n", c, (cf->reader.tid).raw);

	// add data to buffer
	if (cf->reader.rbyte + 1 < cf->reader.nbyte) {
		cf->reader.buf[cf->reader.rbyte] = c;
		cf->reader.buf[cf->reader.rbyte + 1] = '\0';
		cf->reader.rbyte++;
	}

	// if new line or buffer full, return
	if (c == '\n' || cf->reader.rbyte + 1 >= cf->reader.nbyte) {
		*(cf->reader.rval) = cf->reader.rbyte;

		dprintf(2, "*** serial_read_callback: send tid = %d, buf = %s\n, len = %d",
				L4_ThreadNo(cf->reader.tid), cf->reader.buf, *(cf->reader.rval));

		// XXX this is a complete freaking mystery.  Originally we
		// thought that the need to call L4_Reply twice was because
		// we were using UncachedMemory for everything - and that
		// the first call failed but did something magic with the
		// cache to make it work the second time - but now that this
		// has been fixed it STILL doesn't work.
		L4_CacheFlushAll(); // TODO only flush buffer

		dprintf(2, "*** serial_read_callback: send tid = %d, buf = %s\n, len = %d",
				L4_ThreadNo(cf->reader.tid), cf->reader.buf, *(cf->reader.rval));

		L4_MsgTag_t tag = L4_Reply(cf->reader.tid);
		if (L4_IpcFailed(tag)) {
			dprintf(2, "!!! serial_read_callback: reply failed first time (Err %d)!\n",
					L4_ErrorCode());
			tag = L4_Reply(cf->reader.tid);
			if (L4_IpcFailed(tag)) {
				dprintf(2, "!!! serial_read_callback: reply failed second time (Err %d)!\n",
						L4_ErrorCode());
			}
		}

		cf->reader.rbyte = 0;
	}

	dprintf(2, "*** serial read: buf = %s\n", cf->reader.buf);
}


