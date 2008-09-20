#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <l4/ipc.h>
#include <l4/message.h>
#include <l4/types.h>

#include <sos/sos.h>

#define YES_REPLY 1
#define NO_REPLY 0
#define MAGIC_THAT_MAKES_LABELS_WORK 4

/* Standard file descriptors */
fildes_t stdout_fd = 0;
fildes_t stdin_fd = (-1); // never used, grr

/* Standard syscall interface. */
static void prepareSyscall(L4_Msg_t *msg) {
	L4_MsgClear(msg);
}

static void makeSyscall(syscall_t s, int reply, L4_Msg_t *msg) {
	L4_Set_MsgLabel(msg, s << MAGIC_THAT_MAKES_LABELS_WORK);
	L4_MsgLoad(msg);

	if (reply == YES_REPLY) {
		L4_Call(L4_rootserver);
	} else {
		L4_Send(L4_rootserver);
	}
}

/* Misc system calls */

void debug_flush(void) {
	L4_Msg_t msg;
	prepareSyscall(&msg);
	makeSyscall(SOS_DEBUG_FLUSH, NO_REPLY, &msg);
}

void thread_block(void) {
	L4_Msg_t msg;

	L4_MsgClear(&msg);
	L4_MsgTag_t tag = L4_Receive(L4_Myself());

	if (L4_IpcFailed(tag)) {
		printf("!!! thread_block: failed, tag=%lx\n", tag.raw);
	}
}

int moremem(uintptr_t *base, unsigned int nb) {
	int rval;

	L4_Msg_t msg;
	prepareSyscall(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) base);
	L4_MsgAppendWord(&msg, (L4_Word_t) nb);
	L4_MsgAppendWord(&msg, (L4_Word_t) &rval);

	makeSyscall(SOS_MOREMEM, YES_REPLY, &msg);
	return rval;
}

/* I/O system calls */

/* 
 * Open file and return file descriptor, -1 if unsuccessful 
 * (too many open files, console already open for reading).
 * A new file should be created if 'path' does not already exist.
 * A failed attempt to open the console for reading (because it is already
 * open) will result in a context switch to reduce the cost of busy waiting
 * for the console.
 * "path" is file name, "mode" is one of O_RDONLY, O_WRONLY, O_RDWR.
 */
fildes_t open(const char *path, fmode_t mode) {
	fildes_t rval;

	L4_Msg_t msg;
	prepareSyscall(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) path);
	L4_MsgAppendWord(&msg, (L4_Word_t) mode);
	L4_MsgAppendWord(&msg, (L4_Word_t) &rval);

	makeSyscall(SOS_OPEN, YES_REPLY, &msg);
	return rval;
}

/* 
 * Closes an open file. Returns 0 if successful, -1 if not (invalid "file").
 */
int close(fildes_t file) {
	int rval;

	L4_Msg_t msg;
	prepareSyscall(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) &rval);

	makeSyscall(SOS_CLOSE, YES_REPLY, &msg);
	return rval;
}

/* 
 * Read from an open file, into "buf", max "nbyte" bytes.
 * Returns the number of bytes read.
 * Will block when reading from console and no input is presently
 * available. Returns -1 on error (invalid file).
 */
int read(fildes_t file, char *buf, size_t nbyte) {
	int rval;

	L4_Msg_t msg;
	prepareSyscall(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) buf);
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);
	L4_MsgAppendWord(&msg, (L4_Word_t) &rval);

	makeSyscall(SOS_READ, YES_REPLY, &msg);
	return rval;
}

/* 
 * Write to an open file, from "buf", max "nbyte" bytes.
 * Returns the number of bytes written. <nbyte disk is full.
 * Returns -1 on error (invalid file).
 */
int write(fildes_t file, const char *buf, size_t nbyte) {
	int rval;

	L4_Msg_t msg;
	prepareSyscall(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) buf);
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);
	L4_MsgAppendWord(&msg, (L4_Word_t) &rval);

	makeSyscall(SOS_WRITE, YES_REPLY, &msg);
	return rval;
}

/* 
 * Reads name of entry "pos" in directory into "name", max "nbyte" bytes.
 * Returns number of bytes returned, zero if "pos" is next free entry,
 * -1 if error (non-existent entry).
 */
int getdirent(int pos, char *name, size_t nbyte) {
	int rval;

	L4_Msg_t msg;
	prepareSyscall(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) pos);
	L4_MsgAppendWord(&msg, (L4_Word_t) name);
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);
	L4_MsgAppendWord(&msg, (L4_Word_t) &rval);

	makeSyscall(SOS_GETDIRENT, YES_REPLY, &msg);
	return rval;
}

/* 
 * Returns information about file "path" through "buf".
 * Returns 0 if successful, -1 otherwise (invalid name).
 */
int stat(const char *path, stat_t *buf) {
	int rval;

	L4_Msg_t msg;
	prepareSyscall(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) path);
	L4_MsgAppendWord(&msg, (L4_Word_t) buf);
	L4_MsgAppendWord(&msg, (L4_Word_t) &rval);

	makeSyscall(SOS_STAT, YES_REPLY, &msg);
	return rval;
}

/* 
 * Create a new process running the executable image "path".
 * Returns ID of new process, -1 if error (non-executable image, nonexisting
 * file).
 */
pid_t process_create(const char *path) {
	printf("process_create: system call not implemented.\n");
	return 0;
}

/* 
 * Delete process (and close all its file descriptors).
 * Returns 0 if successful, -1 otherwise (invalid process).
 */
int process_delete(pid_t pid) {
	printf("process_delete: system call not implemented.\n");
	return 0;
}

/* Returns ID of caller's process. */
pid_t my_id(void) {
	printf("my_id: system call not implemented.\n");
	return 0;
}

/* 
 * Returns through "processes" status of active processes (at most "max"),
 * returns number of process descriptors actually returned.
 */
int process_status(process_t *processes, unsigned max) {
	printf("process_status: system call not implemented.\n");
	return 0;
}

/* 
 * Wait for process "pid" to exit. If "pid" is -1, wait for any process
 * to exit. Returns the pid of the process which exited.
 */
pid_t process_wait(pid_t pid) {
	printf("process_wait: system call not implemented.\n");
	return 0;
}

/* Returns time in microseconds since booting. */
long uptime(void) {
	long rval;
	L4_Msg_t msg;

	prepareSyscall(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) &rval);
	makeSyscall(SOS_TIME_STAMP, YES_REPLY, &msg);

	return rval;
}

/* Sleeps for the specified number of milliseconds. */
void sleep(int msec) {
	L4_Msg_t msg;

	prepareSyscall(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) msec);
	makeSyscall(SOS_SLEEP, YES_REPLY, &msg);
}

/* 
 * Make VM region ["adr","adr"+"size") sharable by other processes.
 * If "writable" is non-zero, other processes may have write access to the
 * shared region. Both, "adr" and "size" must be divisible by the page size.
 *
 * In order for a page to be shared, all participating processes must execute
 * the system call specifying an interval including that page.
 * Once a page is shared, a process may write to it if and only if all
 * _other_ processes have set up the page as shared writable.
 *
 * Returns 0 if successful, -1 otherwise (invalid address or size).
 */
int share_vm(void *adr, size_t size, int writable) {
	printf("share_vm: system call not implemented.\n");
	return -1;
}

