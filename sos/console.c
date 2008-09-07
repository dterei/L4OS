#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "l4.h"
#include "libsos.h"
#include "network.h"
#include "syscall.h"

#define verbose 1

// The file names of our consoles
Console_File Console_Files[] = { {"console", 1, CONSOLE_RW_UNLIMITED, 0, 0} };

VNode
console_init(VNode sflist) {
	int i;

	dprintf(1, "*** console_init: creating special console files ***\n");

	for (i = 0; i < NUM_CONSOLES; i++) {
		dprintf(1, "*** console_init: setting up special file; %s\n", Console_Files[i].path);

		// create new vnode;
		VNode console = (VNode) malloc(sizeof(struct VNode_t));

		// set up console vnode
		console->path = Console_Files[i].path;
		console->vstat.st_type = ST_SPECIAL;
		console->vstat.st_fmode = FM_READ | FM_WRITE;
		console->vstat.st_size = 0;
		console->vstat.st_ctime = 0;
		console->vstat.st_atime = 0;

		// setup system calls
		console->open = console_open;
		console->close = console_close;
		console->read = console_read;
		console->write = console_write;
		console->getdirent = console_getdirent;
		console->stat = console_stat;

		// setup the console struct
		Console_Files[i].reader.tid = L4_nilthread;
		console->extra = (void *) (&Console_Files[i]);

		// add console to special files
		console->next = sflist;
		console->previous = NULL;
		sflist->previous = console;
		sflist = console;
	}

	// register callback
	int r = network_register_serialhandler(serial_read_callback);
	dprintf(1, "*** console_init: register = %d\n", r);

	return sflist;
}

static
void
console_open_finish(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		int *rval, void (*open_done)(L4_ThreadId_t tid, VNode self,
			const char *path, fmode_t mode, int *rval), int r) {
	*rval = r;
	open_done(tid, self, path, mode, rval);
	syscall_reply(tid);
}


void
console_open(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		int *rval, void (*open_done)(L4_ThreadId_t tid, VNode self,
			const char *path, fmode_t mode, int *rval)) {
	dprintf(1, "*** console_open(%s, %d)\n", path, mode);

	// make sure console exists
	if (self == NULL) {
		console_open_finish(tid, self, path, mode, rval, open_done, -1);
		return;
	}

	// make sure they passed in the right vnode
	if (strcmp(self->path, path) != 0) {
		console_open_finish(tid, self, path, mode, rval, open_done, -1);
		return;
	}

	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL) {
		console_open_finish(tid, self, path, mode, rval, open_done, -1);
		return;
	}

	fildes_t fd = -1;

	// open file for reading
	if (mode & FM_READ) {
		// check if reader slots full
		if (cf->readers > cf->Max_Readers) {
			console_open_finish(tid, self, path, mode, rval, open_done, -1);
			return;
		} else {
			fd = findNextFd(L4_SpaceNo(L4_SenderSpace()));
			if (fd < 0) {
				console_open_finish(tid, self, path, mode, rval, open_done, -1);
				return;
			}
			cf->readers++;
		}
	}

	// open file for writing
	if (mode & FM_WRITE) {
		// check if writers slots full
		if (cf->writers > cf->Max_Writers) {
			if (mode & FM_READ) {
				cf->readers--;
			}
			console_open_finish(tid, self, path, mode, rval, open_done, -1);
			return;
		} else {
			if (fd == -1) {
				fd = findNextFd(L4_SpaceNo(L4_SenderSpace()));
				if (fd < 0) {
					console_open_finish(tid, self, path, mode, rval, open_done, -1);
					return;
				}
			}
			cf->writers++;
		}
	}

	console_open_finish(tid, self, path, mode, rval, open_done, fd);
}

static
void
console_close_finish(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
		int *rval, void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
			int *rval), int r) {
	*rval = r;
	close_done(tid, self, file, mode, rval);
	syscall_reply(tid);
}

void
console_close(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
		int *rval, void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
			int *rval)) {
	dprintf(1, "*** console_close: %d\n", file);

	// make sure console exists
	if (self == NULL) {
		console_close_finish(tid, self, file, mode, rval, close_done, -1);
		return;
	}

	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL) {
		console_close_finish(tid, self, file, mode, rval, close_done, -1);
		return;
	}

	// decrease counts
	if (mode & FM_WRITE) {
		cf->writers--;
	}
	if (mode & FM_READ) {
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

	// 'close' vnode if no one has it open
	if (cf->writers <= 0 && cf->readers <= 0 && self->refcount == 1) {
		self->refcount = 0;
		cf->writers = 0;
		cf->readers = 0;
		self = NULL;
	}

	console_close_finish(tid, self, file, mode, rval, close_done, 0);
}

void
console_read(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, int *rval) {

	dprintf(1, "*** console_read: %d, %p, %d from %p\n", file, buf, nbyte, tid.raw);

	// make sure console exists
	if (self == NULL) {
		*rval = (-1);
		return;
	}

	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL) {
		*rval = (-1);
		return;
	}

	// XXX for some reason this causes a page fault
	//if (L4_IsNilThread(cf->reader.tid)) {
		//*rval = (-1);
		//return;
	//}

	// store read request
	cf->reader.tid = tid;
	cf->reader.buf = buf;
	cf->reader.rval = rval;
	cf->reader.nbyte = nbyte;
	cf->reader.rbyte = 0;
	*rval = 0;
}

void
console_write(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int *rval) {

	dprintf(1, "*** console_write: %d %p %d\n", file, buf, nbyte);

	// because it doesn't like a const
	// XXX Need to make sure we don't block up sos too long.
	// either use a thread just for writes or continuations.
	char *buf2 = (char *)buf;
	*rval = network_sendstring_char(nbyte, buf2);
	syscall_reply(tid);
}

void
console_getdirent(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte,
		int *rval) {
	dprintf(1, "*** console_getdirent: %d, %s, %d\n", pos, name, nbyte);

	dprintf(0, "***console_getdirent: Not implemented for console fs\n");

   *rval = (-1);
	syscall_reply(tid);
}

void
console_stat(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf, int *rval) {
	dprintf(1, "*** console_stat: %s, %d, %d, %d, %d, %d\n", path, buf->st_type,
			buf->st_fmode, buf->st_size, buf->st_ctime, buf->st_atime);

	dprintf(0, "***console_stat: Not implemented for console fs\n");

   *rval = (-1);
	syscall_reply(tid);
}

void
serial_read_callback(struct serial *serial, char c) {
	dprintf(1, "*** serial_read_callback: %c\n", c);

	// TODO hack, need proper way of handling finding if we are
	// going be able to handle multiple serial devices.
	Console_File *cf = &Console_Files[0];
	if (cf == NULL || L4_IsNilThread(cf->reader.tid)) {
		return;
	}

	dprintf(2, "*** serial_read_callback: %c, send to %p\n", c, (cf->reader.tid).raw);

	// add data to buffer
	if (cf->reader.rbyte < cf->reader.nbyte) {
		cf->reader.buf[cf->reader.rbyte] = c;
		cf->reader.rbyte++;
	}

	// if new line or buffer full, return
	if (c == '\n' || cf->reader.rbyte >= cf->reader.nbyte) {
		*(cf->reader.rval) = cf->reader.rbyte;

		dprintf(2, "*** serial_read_callback: send tid = %d, buf = %s\n, len = %d",
				L4_ThreadNo(cf->reader.tid), cf->reader.buf, *(cf->reader.rval));

		dprintf(2, "*** serial_read_callback: send tid = %d, buf = %s\n, len = %d",
				L4_ThreadNo(cf->reader.tid), cf->reader.buf, *(cf->reader.rval));

		syscall_reply(cf->reader.tid);

		// remove request now its done
		cf->reader.tid = L4_nilthread;
		cf->reader.rval = NULL;
		cf->reader.buf = NULL;
		cf->reader.nbyte = 0;
		cf->reader.rbyte = 0;
	}

	dprintf(2, "*** serial read: buf = %s\n", cf->reader.buf);
}

