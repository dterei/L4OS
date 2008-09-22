#include <string.h>

#include "console.h"

#include "libsos.h"
#include "network.h"
#include "syscall.h"

#define verbose 1

// How many consoles we have
#define NUM_CONSOLES 1

// Unlimited Console readers or writers value
#define CONSOLE_RW_UNLIMITED ((unsigned int) (-1))

// struct for storing console read requests (continuation struct)
typedef struct {
	L4_ThreadId_t tid;
	fildes_t file;
	char *buf;
	size_t nbyte;
	size_t rbyte;
	int *rval;
	void (*read_done)(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, int *rval);
} Console_ReadRequest;

// struct for storing info about a console file
typedef struct {
	// store console device info
	VNode vnode;
	char *path;
	const unsigned int Max_Readers;
	const unsigned int Max_Writers;

	// store info about a thread waiting on a read
	// change to a list to support more then one reader
	Console_ReadRequest reader;	
} Console_File;

// The file names of our consoles
Console_File Console_Files[] = { {NULL, "console", 1, CONSOLE_RW_UNLIMITED } };

// callback for read
static void serial_read_callback(struct serial *serial, char c);

/* Initialise all console devices adding them to the special file list. */
VNode
console_init(VNode sflist) {
	dprintf(1, "*** console_init: creating special console files ***\n");

	int i;
	for (i = 0; i < NUM_CONSOLES; i++) {
		dprintf(2, "*** console_init: setting up special file; %s\n", Console_Files[i].path);

		// create new vnode;
		VNode console = (VNode) malloc(sizeof(struct VNode_t));

		// set up console vnode
		strncpy(console->path, Console_Files[i].path, N_NAME);
		console->vstat.st_type = ST_SPECIAL;
		console->vstat.st_fmode = FM_READ | FM_WRITE;
		console->vstat.st_size = 0;
		console->vstat.st_ctime = 0;
		console->vstat.st_atime = 0;
		console->readers = 0;
		console->writers = 0;
		console->Max_Readers = Console_Files[i].Max_Readers;
		console->Max_Writers = Console_Files[i].Max_Writers;

		// setup system calls
		console->open = console_open;
		console->close = console_close;
		console->read = console_read;
		console->write = console_write;
		console->getdirent = console_getdirent;
		console->stat = console_stat;
		console->remove = console_remove;

		// setup the console struct
		Console_Files[i].reader.tid = L4_nilthread;
		Console_Files[i].vnode = console;
		console->extra = (void *) (&Console_Files[i]);

		// add console to special files
		console->next = NULL;
		console->previous = NULL;
		// add to list if list not empty
		if (sflist != NULL) {
			console->next = sflist;
			sflist->previous = console;
		}
		sflist = console;
	}

	// register callback
	int r = network_register_serialhandler(serial_read_callback);
	dprintf(1, "*** console_init: register = %d\n", r);

	return sflist;
}

static
void
open_finish(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		int *rval, void (*open_done)(L4_ThreadId_t tid, VNode self,
			const char *path, fmode_t mode, int *rval), int r) {
	*rval = r;
	open_done(tid, self, path, mode, rval);
	syscall_reply(tid, *rval);
}

/* Open a console file */
void
console_open(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		int *rval, void (*open_done)(L4_ThreadId_t tid, VNode self,
			const char *path, fmode_t mode, int *rval)) {
	dprintf(1, "*** console_open(%s, %d)\n", path, mode);

	// make sure console exists
	if (self == NULL) {
		open_finish(tid, self, path, mode, rval, open_done, SOS_VFS_NOVNODE);
		return;
	}

	// make sure they passed in the right vnode
	if (strcmp(self->path, path) != 0) {
		open_finish(tid, self, path, mode, rval, open_done, SOS_VFS_NOVNODE);
		return;
	}

	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL) {
		open_finish(tid, self, path, mode, rval, open_done, SOS_VFS_CORVNODE);
		return;
	}

	open_finish(tid, self, path, mode, rval, open_done, SOS_VFS_OK);
}

static
void
console_close_finish(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
		int *rval, void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
			int *rval), int r) {
	dprintf(1, "*** console_open_done (%d)\n", *rval);

	*rval = r;
	close_done(tid, self, file, mode, rval);
	syscall_reply(tid, *rval);
}

/* Close a console file */
void
console_close(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
		int *rval, void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
			int *rval)) {
	dprintf(1, "*** console_close: %d\n", file);

	// make sure console exists
	if (self == NULL) {
		console_close_finish(tid, self, file, mode, rval, close_done, SOS_VFS_NOVNODE);
		return;
	}

	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL) {
		console_close_finish(tid, self, file, mode, rval, close_done, SOS_VFS_CORVNODE);
		return;
	}

	// decrease counts
	if (mode & FM_WRITE) {
		self->writers--;
	}
	if (mode & FM_READ) {
		self->readers--;
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
	if (self->writers <= 0 && self->readers <= 0 && self->refcount == 1) {
		self->refcount = 0;
		self->writers = 0;
		self->readers = 0;
		self = NULL;
	}

	console_close_finish(tid, self, file, mode, rval, close_done, SOS_VFS_OK);
}

/* Read from a console file */
void
console_read(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, int *rval, void (*read_done)(L4_ThreadId_t tid,
			VNode self, fildes_t file, L4_Word_t pos, char *buf, size_t nbyte, int *rval)) {
	dprintf(1, "*** console_read: %d, %p, %d from %p\n", file, buf, nbyte, tid.raw);

	// make sure console exists
	if (self == NULL) {
		*rval = SOS_VFS_NOVNODE;
		return;
	}

	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL) {
		*rval = SOS_VFS_CORVNODE;
		return;
	}

	// XXX for some reason this causes a page fault
	//if (L4_IsNilThread(cf->reader.tid)) {
		//*rval = SOS_VFS_ERROR;
		//return;
	//}

	// store read request
	cf->reader.tid = tid;
	cf->reader.file = file;
	cf->reader.buf = buf;
	cf->reader.nbyte = nbyte;
	cf->reader.rbyte = 0;
	cf->reader.rval = rval;
	cf->reader.read_done = read_done;
	*rval = SOS_VFS_OK;
}

/* Write to a console file */
void
console_write(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
			const char *buf, size_t nbyte, int *rval, void (*write_done)(L4_ThreadId_t tid,
				VNode self, fildes_t file, L4_Word_t offset, const char *buf, size_t nbyte,
				int *rval)) {
	dprintf(1, "*** console_write: %d %p %d\n", file, buf, nbyte);

	// because it doesn't like a const
	// XXX Need to make sure we don't block up sos too long.
	// either use a thread just for writes or continuations.
	*rval = network_sendstring_char(nbyte, (char *) buf);
	write_done(tid, self, file, offset, buf, 0, rval);
	syscall_reply(tid, *rval);
}

/* Get a directory listing */
void
console_getdirent(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte,
		int *rval) {
	dprintf(1, "*** console_getdirent: %d, %s, %d\n", pos, name, nbyte);

	dprintf(0, "***console_getdirent: Not implemented for console fs\n");

   *rval = SOS_VFS_NOTIMP;
	syscall_reply(tid, *rval);
}

/* Stat a file */
void
console_stat(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf, int *rval) {
	dprintf(1, "*** console_stat: %s, %d, %d, %d, %d, %d\n", path, buf->st_type,
			buf->st_fmode, buf->st_size, buf->st_ctime, buf->st_atime);

	dprintf(0, "***console_stat: Not implemented for console fs\n");

   *rval = SOS_VFS_NOTIMP;
	syscall_reply(tid, *rval);
}

/* Remove a file */
void
console_remove(L4_ThreadId_t tid, VNode self, const char *path, int *rval) {
	dprintf(1, "*** console_remove: %d %s ***\n", L4_ThreadNo(tid), path);

	dprintf(0, "***console_remove: Not implemented for console fs\n");

	*rval = SOS_VFS_NOTIMP;
	syscall_reply(tid, *rval);
}

/* Callback from Serial Library for Reads */
static
void
serial_read_callback(struct serial *serial, char c) {
	dprintf(1, "*** serial_read_callback: %c\n", c);

	// TODO hack, need proper way of handling finding if we are
	// going be able to handle multiple serial devices.
	Console_ReadRequest *rq = &(Console_Files[0].reader);
	if (L4_IsNilThread(rq->tid)) {
		return;
	}

	dprintf(2, "*** serial_read_callback: %c, send to %p\n", c, (rq->tid).raw);

	// add data to buffer
	if (rq->rbyte < rq->nbyte) {
		rq->buf[rq->rbyte] = c;
		rq->rbyte++;
	}

	// if new line or buffer full, return
	if (c == '\n' || rq->rbyte >= rq->nbyte) {
		*(rq->rval) = rq->rbyte;

		dprintf(2, "*** serial_read_callback: send tid = %d, buf = %s\n, len = %d",
				L4_ThreadNo(rq->tid), rq->buf, *(rq->rval));

		dprintf(2, "*** serial_read_callback: send tid = %d, buf = %s\n, len = %d",
				L4_ThreadNo(rq->tid), rq->buf, *(rq->rval));

		rq->read_done(rq->tid, NULL, rq->file, 0, rq->buf, 0, rq->rval);
		syscall_reply(rq->tid, *(rq->rval));

		// remove request now its done
		rq->tid = L4_nilthread;
		rq->file = (fildes_t) (-1);
		rq->rval = NULL;
		rq->buf = NULL;
		rq->nbyte = 0;
		rq->rbyte = 0;
	}

	dprintf(2, "*** serial read: buf = %s\n", rq->buf);
}

