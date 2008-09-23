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

fildes_t stdout_fd = 0;
fildes_t stdin_fd = (-1); // never used, grr

char *syscall_show(syscall_t syscall) {
	switch (syscall) {
		case SOS_REPLY: return "SOS_REPLY";
		case SOS_KERNEL_PRINT: return "SOS_KERNEL_PRINT";
		case SOS_DEBUG_FLUSH: return "SOS_DEBUG_FLUSH";
		case SOS_MOREMEM: return "SOS_MOREMEM";
		case SOS_COPYIN: return "SOS_COPYIN";
		case SOS_COPYOUT: return "SOS_COPYOUT";
		case SOS_OPEN: return "SOS_OPEN";
		case SOS_CLOSE: return "SOS_CLOSE";
		case SOS_READ: return "SOS_READ";
		case SOS_WRITE: return "SOS_WRITE";
		case SOS_GETDIRENT: return "SOS_GETDIRENT";
		case SOS_STAT: return "SOS_STAT";
		case SOS_REMOVE: return "SOS_REMOVE";
		case SOS_PROCESS_CREATE: return "SOS_PROCESS_CREATE";
		case SOS_PROCESS_DELETE: return "SOS_PROCESS_DELETE";
		case SOS_MY_ID: return "SOS_MY_ID";
		case SOS_PROCESS_STATUS: return "SOS_PROCESS_STATUS";
		case SOS_PROCESS_WAIT: return "SOS_PROCESS_WAIT";
		case SOS_TIME_STAMP: return "SOS_TIME_STAMP";
		case SOS_USLEEP: return "SOS_USLEEP";
		case SOS_SHARE_VM: return "SOS_SHARE_VM";
	}

	return "UNRECOGNISED";
}

static void prepareSyscall(L4_Msg_t *msg) {
	L4_MsgClear(msg);
}

static L4_Word_t makeSyscall(syscall_t s, int reply, L4_Msg_t *msg) {
	L4_MsgTag_t tag;
	L4_Msg_t rMsg;

	L4_Set_MsgLabel(msg, s << MAGIC_THAT_MAKES_LABELS_WORK);
	L4_MsgLoad(msg);

	if (reply == YES_REPLY) {
		tag = L4_Call(L4_rootserver);
	} else {
		tag = L4_Send(L4_rootserver);
	}

	L4_MsgStore(tag, &rMsg);
	return L4_MsgWord(&rMsg, 0);
}

void kprint(char *str) {
	copyin(str, strlen(str) + 1, 0);

	L4_Msg_t msg;
	prepareSyscall(&msg);
	makeSyscall(SOS_KERNEL_PRINT, NO_REPLY, &msg);
}

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
	L4_Msg_t msg;
	prepareSyscall(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) base);
	L4_MsgAppendWord(&msg, (L4_Word_t) nb);

	int rval = makeSyscall(SOS_MOREMEM, YES_REPLY, &msg);

	if (rval == 0) {
		return 0; // no memory
	} else {
		copyout(base, sizeof(uintptr_t), 0);
		return rval;
	}
}

void copyin(void *data, size_t size, int append) {
	L4_Msg_t msg;
	prepareSyscall(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) data);
	L4_MsgAppendWord(&msg, (L4_Word_t) size);
	L4_MsgAppendWord(&msg, (L4_Word_t) append);

	makeSyscall(SOS_COPYIN, YES_REPLY, &msg);
}

void copyout(void *data, size_t size, int append) {
	L4_Msg_t msg;
	prepareSyscall(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) data);
	L4_MsgAppendWord(&msg, (L4_Word_t) size);
	L4_MsgAppendWord(&msg, (L4_Word_t) append);

	makeSyscall(SOS_COPYOUT, YES_REPLY, &msg);
}

fildes_t open(const char *path, fmode_t mode) {
	copyin((void*) path, strlen(path) + 1, 0);

	L4_Msg_t msg;
	prepareSyscall(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) mode);

	return makeSyscall(SOS_OPEN, YES_REPLY, &msg);
}

int close(fildes_t file) {
	L4_Msg_t msg;
	prepareSyscall(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) file);

	return makeSyscall(SOS_CLOSE, YES_REPLY, &msg);
}

int read(fildes_t file, char *buf, size_t nbyte) {
	int rval;
	L4_Msg_t msg;
	prepareSyscall(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);

	rval = makeSyscall(SOS_READ, YES_REPLY, &msg);

	copyout(buf, nbyte, 0);

	return rval;
}

int write(fildes_t file, const char *buf, size_t nbyte) {
	copyin((void*) buf, nbyte, 0);

	L4_Msg_t msg;
	prepareSyscall(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);

	return makeSyscall(SOS_WRITE, YES_REPLY, &msg);
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
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);

	rval = makeSyscall(SOS_GETDIRENT, YES_REPLY, &msg);

	copyout((void*) name, nbyte, 0);

	return rval;
}

/* 
 * Returns information about file "path" through "buf".
 * Returns 0 if successful, -1 otherwise (invalid name).
 */
int stat(const char *path, stat_t *buf) {
	int len = strlen(path);
	copyin((void*) path, len + 1, 0);

	int rval;
	L4_Msg_t msg;
	prepareSyscall(&msg);

	rval = makeSyscall(SOS_STAT, YES_REPLY, &msg);

	// The copyin could have left the position not word
	// aligned however SOS will copy the stat info into
	// the next word aligned position - so must compensate.
	int offset = (len + 1) % sizeof(L4_Word_t);

	if (offset > 0) {
		copyout(buf, sizeof(L4_Word_t) - offset, 1);
	}

	copyout(buf, sizeof(stat_t), 1);

	return rval;
}

/* Removees the specified file "path".
 * Returns - if successful, -1 otherwise (invalid name).
 */
int fremove(const char *path) {
	int len = strlen(path);
	copyin((void*) path, len + 1, 0);

	int rval;
	L4_Msg_t msg;
	prepareSyscall(&msg);

	rval = makeSyscall(SOS_REMOVE, YES_REPLY, &msg);

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
	L4_Msg_t msg;
	prepareSyscall(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) pid);

	makeSyscall(SOS_PROCESS_DELETE, NO_REPLY, &msg);
	return 0;
}

/* Returns ID of caller's process. */
pid_t my_id(void) {
	L4_Msg_t msg;
	prepareSyscall(&msg);
	return makeSyscall(SOS_MY_ID, YES_REPLY, &msg);
}

/* 
 * Returns through "processes" status of active processes (at most "max"),
 * returns number of process descriptors actually returned.
 */
int process_status(process_t *processes, unsigned max) {
	L4_Msg_t msg;
	int rval;

	prepareSyscall(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) max);
	rval = makeSyscall(SOS_PROCESS_STATUS, YES_REPLY, &msg);

	copyout(processes, rval * sizeof(process_t), 0);

	return rval;
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
	L4_Msg_t msg;
	prepareSyscall(&msg);
	return makeSyscall(SOS_TIME_STAMP, YES_REPLY, &msg);
}

/* Sleeps for the specified number of microseconds. */
void usleep(int msec) {
	L4_Msg_t msg;

	prepareSyscall(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) msec);
	makeSyscall(SOS_USLEEP, YES_REPLY, &msg);
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

