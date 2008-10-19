#include <string.h>

#include "console.h"

#include "libsos.h"
#include "network.h"
#include "process.h"
#include "syscall.h"

#define verbose 1

// How many consoles we have
#define NUM_CONSOLES 1
#define CONSOLE_STAT { (ST_SPECIAL), (FM_READ | FM_WRITE), (0), (0), (0) }

// struct for storing console read requests (continuation struct)
typedef struct {
	pid_t pid;
	fildes_t file;
	char *buf;
	size_t nbyte;
	size_t rbyte;
	void (*read_done)(pid_t pid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, int status);
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

	// use a buffer to 'fix' crappy provided network code that doesn't have queues
	char buf[CONSOLE_BUF_SIZ];
	int buf_used;
} Console_File;

// The file names of our consoles
Console_File Console_Files[] = { {NULL, "console", 1, FM_UNLIMITED_RW } };

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
		strncpy(console->path, Console_Files[i].path, MAX_FILE_NAME);
		stat_t st = CONSOLE_STAT;
		memcpy(&(console->vstat), &st, sizeof(stat_t));
		console->readers = 0;
		console->writers = 0;
		console->Max_Readers = Console_Files[i].Max_Readers;
		console->Max_Writers = Console_Files[i].Max_Writers;

		// setup system calls
		console->open = console_open;
		console->close = console_close;
		console->read = console_read;
		console->write = console_write;
		console->flush = console_flush;
		console->getdirent = console_getdirent;
		console->stat = console_stat;
		console->remove = console_remove;

		// setup the console struct
		Console_Files[i].reader.pid = NIL_PID;
		Console_Files[i].vnode = console;
		Console_Files[i].buf_used = 0;
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

/* Open a console file */
void
console_open(pid_t pid, VNode self, const char *path, fmode_t mode,
		void (*open_done)(pid_t pid, VNode self, fmode_t mode, int status)) {
	dprintf(1, "*** console_open(%s, %d)\n", path, mode);
	dprintf(0, "!!! console_open: Not implemented for console fs\n");
	open_done(pid, self, mode, SOS_VFS_NOTIMP);
}

/* Close a console file */
void
console_close(pid_t pid, VNode self, fildes_t file, fmode_t mode,
		void (*close_done)(pid_t pid, VNode self, fildes_t file, fmode_t mode, int status)) {
	dprintf(1, "*** console_close: %d\n", file);
	dprintf(0, "!!! console_close: Not implemented for console fs\n");
	close_done(pid, self, file, mode, SOS_VFS_NOTIMP);
}

/* Read from a console file */
void
console_read(pid_t pid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, void (*read_done)(pid_t pid,
			VNode self, fildes_t file, L4_Word_t pos, char *buf, size_t nbyte, int status)) {
	dprintf(1, "*** console_read: %d, %p, %d from %p\n", file, buf, nbyte, pid);

	// make sure console exists
	if (self == NULL) {
		dprintf(1, "!!! console_read: console doesn't exist\n");
		read_done(pid, self, file, pos, buf, 0, SOS_VFS_NOVNODE);
		return;
	}

	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL) {
		dprintf(0, "!!! VNode without Console_File (%p) passed into console_read\n", cf);
		read_done(pid, self, file, pos, buf, 0, SOS_VFS_CORVNODE);
		return;
	}

	if (cf->reader.pid != NIL_PID) {
		dprintf(1, "!!! console_read: already a reader\n");
		read_done(pid, self, file, pos, buf, 0, SOS_VFS_READFULL);
		return;
	}

	// store read request
	cf->reader.pid = pid;
	cf->reader.file = file;
	cf->reader.buf = buf;
	cf->reader.nbyte = nbyte;
	cf->reader.rbyte = 0;
	cf->reader.read_done = read_done;
}

static
int
_flush(Console_File *cf) {
	int status = network_sendstring(cf->buf, cf->buf_used);
	cf->buf_used = 0;
	return status;
}

/* Write to a console file */
void
console_write(pid_t pid, VNode self, fildes_t file, L4_Word_t offset,
			const char *buf, size_t nbyte, void (*write_done)(pid_t pid, VNode self,
				fildes_t file, L4_Word_t offset, const char *buf, size_t nbyte, int status)) {
	dprintf(1, "*** console_write: %d %p %d\n", file, buf, nbyte);
	
	// make sure console exists
	if (self == NULL) {
		write_done(pid, self, file, offset, buf, 0, SOS_VFS_NOVNODE);
		return;
	}

	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL) {
		dprintf(0, "!!! VNode without Console_File (%p) passed into console_write\n", cf);
		write_done(pid, self, file, offset, buf, 0, SOS_VFS_CORVNODE);
		return;
	}

	int status = nbyte;
	for (int i = 0; i < nbyte; ) {
		while (cf->buf_used < CONSOLE_BUF_SIZ && i < nbyte) {
			cf->buf[cf->buf_used] = buf[i];
			cf->buf_used++;
			i++;
		}

		// console buffer full or user buffer empty or ends in '\n'
		if (cf->buf_used >= CONSOLE_BUF_SIZ || buf[nbyte - 1] == '\n') {
			dprintf(2, "flushing console buffer (%d)\n", cf->buf_used);
			status = _flush(cf);
		}

		if (status < 0) {
			status = SOS_VFS_ERROR;
		} else {
			status = nbyte;
		}
	}

	write_done(pid, self, file, offset, buf, 0, status);
}

/* Flush the given console stream to the network */
void console_flush(pid_t pid, VNode self, fildes_t file) {
	dprintf(1, "*** console_flush: %d, %p, %d\n", pid, self, file);

	// make sure console exists
	if (self == NULL) {
		return;
	}

	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL) {
		dprintf(0, "!!! VNode without Console_File (%p) passed into console_flush\n", cf);
		return;
	}

	dprintf(2, "flushing console buffer (%d)\n", cf->buf_used);
	int status = _flush(cf);

	if (status > 0) {
		syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_OK);
	} else {
		syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_EOF);
	}
}

/* Get a directory listing */
void
console_getdirent(pid_t pid, VNode self, int pos, char *name, size_t nbyte) {
	dprintf(1, "*** console_getdirent: %d, %s, %d\n", pos, name, nbyte);
	dprintf(0, "!!! console_getdirent: Not implemented for console fs\n");
	syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_NOTIMP);
}

/* Stat a file */
void
console_stat(pid_t pid, VNode self, const char *path, stat_t *buf) {
	dprintf(1, "*** console_stat: %d, %p, %s, %p\n", pid, self, path, buf);

	// make sure console exists
	if (self == NULL) {
		syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_NOVNODE);
		return;
	}

	Console_File *cf = (Console_File *) (self->extra);
	if (cf == NULL) {
		dprintf(0, "!!! VNode without Console_File (%p) passed into console_stat\n", cf);
		syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_CORVNODE);
		return;
	}

	memcpy(buf, &(self->vstat), sizeof(stat_t));
	syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_OK);
}

/* Remove a file */
void
console_remove(pid_t pid, VNode self, const char *path) {
	dprintf(1, "*** console_remove: %d %s ***\n", pid, path);
	dprintf(0, "!!! console_remove: Not implemented for console fs\n");
	syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_NOTIMP);
}

/* Callback from Serial Library for Reads */
static
void
serial_read_callback(struct serial *serial, char c) {
	dprintf(1, "*** serial_read_callback: %c\n", c);

	// TODO hack, need proper way of handling finding if we are
	// going be able to handle multiple serial devices.
	Console_ReadRequest *rq = &(Console_Files[0].reader);
	if (rq->pid == NIL_PID) {
		return;
	}

	// add data to buffer
	if (rq->rbyte < rq->nbyte) {
		rq->buf[rq->rbyte] = c;
		rq->rbyte++;
	}

	// if new line or buffer full, return
	if (c == '\n' || rq->rbyte >= rq->nbyte) {
		dprintf(2, "*** serial_read_callback: send pid = %d, buf = %p\n, len = %d",
				rq->pid, rq->buf, rq->rbyte);

		rq->read_done(rq->pid, NULL, rq->file, 0, rq->buf, 0, rq->rbyte);

		// remove request now its done
		rq->pid = NIL_PID;
		rq->file = VFS_NIL_FILE;
		rq->buf = NULL;
		rq->nbyte = 0;
		rq->rbyte = 0;
	}

	dprintf(2, "*** serial read: buf = %p\n", rq->buf);
}

