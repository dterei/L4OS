#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <l4/ipc.h>
#include <l4/message.h>
#include <l4/types.h>

#include <sos/sos.h>

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
		case SOS_FLUSH: return "SOS_FLUSH";
		case SOS_LSEEK: return "SOS_LSEEK";
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
		case SOS_MEMUSE: return "SOS_MEMUSE";
		case SOS_SWAPUSE: return "SOS_SWAPUSE";
		case SOS_PHYSUSE: return "SOS_PHYSUSE";
		case SOS_VPAGER: return "SOS_VPAGER";
		case SOS_MEMLOC: return "SOS_MEMLOC";
		case SOS_SHARE_VM: return "SOS_SHARE_VM";
		case L4_PAGEFAULT: return "L4_PAGEFAULT";
		case L4_INTERRUPT: return "L4_INTERRUPT";
		case L4_EXCEPTION: return "L4_EXCEPTION";
	}

	return "UNRECOGNISED";
}

void syscall_prepare(L4_Msg_t *msg) {
	L4_MsgClear(msg);
}

void syscall_generic(L4_ThreadId_t tid, syscall_t s, int reply,
		L4_Word_t *rvals, int nRvals, L4_Msg_t *msg) {
	L4_MsgTag_t tag;

	L4_Set_MsgLabel(msg, s << MAGIC_THAT_MAKES_LABELS_WORK);
	L4_MsgLoad(msg);

	if (reply == YES_REPLY) {
		tag = L4_Call(tid);
	} else {
		tag = L4_Send(tid);
	}

	L4_MsgStore(tag, msg);

	for (int i = 0; i < nRvals; i++) {
		rvals[i] = L4_MsgWord(msg, 0);
	}
}

L4_Word_t syscall(L4_ThreadId_t tid, syscall_t s, int reply, L4_Msg_t *msg) {
	L4_Word_t rval;
	syscall_generic(tid, s, reply, &rval, 1, msg);
	return rval;
}

void kprint(char *str) {
	copyin(str, strlen(str) + 1, 0);

	L4_Msg_t msg;
	syscall_prepare(&msg);
	syscall(L4_rootserver, SOS_KERNEL_PRINT, NO_REPLY, &msg);
}

void debug_flush(void) {
	L4_Msg_t msg;
	syscall_prepare(&msg);
	syscall(vpager(), SOS_DEBUG_FLUSH, NO_REPLY, &msg);
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
	syscall_prepare(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) nb);

	int rval = syscall(vpager(), SOS_MOREMEM, YES_REPLY, &msg);

	if (rval == 0) {
		return 0; // no memory
	} else {
		copyout(base, sizeof(uintptr_t), 0);
		return rval;
	}
}

void copyin(void *data, size_t size, int append) {
	L4_Msg_t msg;
	syscall_prepare(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) data);
	L4_MsgAppendWord(&msg, (L4_Word_t) size);
	L4_MsgAppendWord(&msg, (L4_Word_t) append);

	syscall(vpager(), SOS_COPYIN, YES_REPLY, &msg);
}

void copyout(void *data, size_t size, int append) {
	L4_Msg_t msg;
	syscall_prepare(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) data);
	L4_MsgAppendWord(&msg, (L4_Word_t) size);
	L4_MsgAppendWord(&msg, (L4_Word_t) append);

	syscall(vpager(), SOS_COPYOUT, YES_REPLY, &msg);
}

fildes_t open(const char *path, fmode_t mode) {
	return open_lock(path, mode, FM_UNLIMITED_RW, FM_UNLIMITED_RW);
}

void openNonblocking(const char *path, fmode_t mode) {
	open_lockNonblocking(path, mode, FM_UNLIMITED_RW, FM_UNLIMITED_RW);
}

fildes_t open_lock(const char *path, fmode_t mode, unsigned int readers,
		unsigned int writers) {
	if (path != NULL) {
		copyin((void*) path, strlen(path) + 1, 0);
	}

	L4_Msg_t msg;
	syscall_prepare(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) mode);
	L4_MsgAppendWord(&msg, (L4_Word_t) readers);
	L4_MsgAppendWord(&msg, (L4_Word_t) writers);

	return syscall(L4_rootserver, SOS_OPEN, YES_REPLY, &msg);
}

void open_lockNonblocking(const char *path, fmode_t mode, unsigned int readers,
		unsigned int writers) {
	if (path != NULL) {
		copyin((void*) path, strlen(path) + 1, 0);
	}

	L4_Msg_t msg;
	syscall_prepare(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) mode);
	L4_MsgAppendWord(&msg, (L4_Word_t) readers);
	L4_MsgAppendWord(&msg, (L4_Word_t) writers);

	syscall(L4_rootserver, SOS_OPEN, NO_REPLY, &msg);
}

int close(fildes_t file) {
	L4_Msg_t msg;
	syscall_prepare(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) file);

	return syscall(L4_rootserver, SOS_CLOSE, YES_REPLY, &msg);
}

void closeNonblocking(fildes_t file) {
	L4_Msg_t msg;

	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, file);

	syscall(L4_rootserver, SOS_CLOSE, NO_REPLY, &msg);
}

int read(fildes_t file, char *buf, size_t nbyte) {
	// flush stdout
	flush(stdout_fd);

	int rval;
	L4_Msg_t msg;
	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);

	rval = syscall(L4_rootserver, SOS_READ, YES_REPLY, &msg);

	copyout(buf, nbyte, 0);

	return rval;
}

void readNonblocking(fildes_t file, size_t nbyte) {
	L4_Msg_t msg;

	// the actual buffer will be the normal copyin buffer
	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);

	syscall(L4_rootserver, SOS_READ, NO_REPLY, &msg);
}

int write(fildes_t file, const char *buf, size_t nbyte) {
	copyin((void*) buf, nbyte, 0);

	L4_Msg_t msg;
	syscall_prepare(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);

	return syscall(L4_rootserver, SOS_WRITE, YES_REPLY, &msg);
}

void writeNonblocking(fildes_t file, size_t nbyte) {
	L4_Msg_t msg;

	// the actual buffer will be the normal copyin buffer
	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);

	syscall(L4_rootserver, SOS_WRITE, NO_REPLY, &msg);
}

/* Flush a file or stream out to disk/network */
int flush(fildes_t file) {
	L4_Msg_t msg;
	syscall_prepare(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) file);

	return syscall(L4_rootserver, SOS_FLUSH, YES_REPLY, &msg);
}

/* Flush a file or stream out to disk/network */
void flushNonblocking(fildes_t file) {
	L4_Msg_t msg;
	syscall_prepare(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) file);

	syscall(L4_rootserver, SOS_FLUSH, NO_REPLY, &msg);
}

/* Lseek sets the file position indicator to the specified position "pos".
 * if "whence" is set to SEEK_SET, SEEK_CUR, or SEEK_END the offset is relative
 * to the start of the file, current position in the file or end of the file
 * respectively.
 *
 * Note: SEEK_END not supported.
 *
 * Returns 0 on success and -1 on error.
 */
int lseek(fildes_t file, fpos_t pos, int whence) {
	L4_Msg_t msg;
	syscall_prepare(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) pos);
	L4_MsgAppendWord(&msg, (L4_Word_t) whence);

	return syscall(L4_rootserver, SOS_LSEEK, YES_REPLY, &msg);
}

void lseekNonblocking(fildes_t file, fpos_t pos, int whence) {
	L4_Msg_t msg;

	// the actual buffer will be the normal copyin buffer
	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) pos);
	L4_MsgAppendWord(&msg, (L4_Word_t) whence);

	syscall(L4_rootserver, SOS_LSEEK, NO_REPLY, &msg);
}

/* 
 * Reads name of entry "pos" in directory into "name", max "nbyte" bytes.
 * Returns number of bytes returned, zero if "pos" is next free entry,
 * -1 if error (non-existent entry).
 */
int getdirent(int pos, char *name, size_t nbyte) {
	int rval;
	L4_Msg_t msg;
	syscall_prepare(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) pos);
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);

	rval = syscall(L4_rootserver, SOS_GETDIRENT, YES_REPLY, &msg);

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
	syscall_prepare(&msg);

	rval = syscall(L4_rootserver, SOS_STAT, YES_REPLY, &msg);

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
	syscall_prepare(&msg);

	rval = syscall(L4_rootserver, SOS_REMOVE, YES_REPLY, &msg);

	return rval;
}

/* 
 * Create a new process running the executable image "path".
 * Returns ID of new process, -1 if error (non-executable image, nonexisting
 * file).
 */
pid_t process_create(const char *path) {
	L4_Msg_t msg;
	int len;

	len = strlen(path);
	copyin((void*) path, len + 1, 0);

	syscall_prepare(&msg);
	return syscall(vpager(), SOS_PROCESS_CREATE, YES_REPLY, &msg);
}

/* 
 * Delete process (and close all its file descriptors).
 * Returns 0 if successful, -1 otherwise (invalid process).
 */
int process_delete(pid_t pid) {
	L4_Msg_t msg;
	syscall_prepare(&msg);

	L4_MsgAppendWord(&msg, (L4_Word_t) pid);

	return syscall(vpager(), SOS_PROCESS_DELETE, YES_REPLY, &msg);
}

/* Returns ID of caller's process. */
pid_t my_id(void) {
	L4_Msg_t msg;
	syscall_prepare(&msg);
	return syscall(L4_rootserver, SOS_MY_ID, YES_REPLY, &msg);
}

/* 
 * Returns through "processes" status of active processes (at most "max"),
 * returns number of process descriptors actually returned.
 */
int process_status(process_t *processes, unsigned max) {
	L4_Msg_t msg;
	int rval;

	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) max);
	rval = syscall(vpager(), SOS_PROCESS_STATUS, YES_REPLY, &msg);

	copyout(processes, rval * sizeof(process_t), 0);

	return rval;
}

/* 
 * Wait for process "pid" to exit. If "pid" is -1, wait for any process
 * to exit. Returns the pid of the process which exited.
 */
pid_t process_wait(pid_t pid) {
	L4_Msg_t msg;
	syscall_prepare(&msg);

	L4_MsgAppendWord(&msg, pid);

	return syscall(vpager(), SOS_PROCESS_WAIT, YES_REPLY, &msg);
}

/* Returns time in microseconds since booting. */
uint64_t uptime(void) {
	L4_Msg_t msg;
	syscall_prepare(&msg);

	uint64_t time = syscall(L4_rootserver, SOS_TIME_STAMP, YES_REPLY, &msg);
	uint64_t hi = L4_MsgWord(&msg, 1);
	time += hi << 32;
	return time;
}

/* Sleeps for the specified number of microseconds. */
void usleep(int usec) {
	L4_Msg_t msg;

	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) usec);
	syscall(L4_rootserver, SOS_USLEEP, YES_REPLY, &msg);
}

/* Get the number of frames in use by user processes */
int memuse(void) {
	L4_Msg_t msg;
	syscall_prepare(&msg);
	return syscall(vpager(), SOS_MEMUSE, YES_REPLY, &msg);
}

int swapuse(void) {
	L4_Msg_t msg;
	syscall_prepare(&msg);
	return syscall(vpager(), SOS_SWAPUSE, YES_REPLY, &msg);
}

int physuse(void) {
	L4_Msg_t msg;
	syscall_prepare(&msg);
	return syscall(vpager(), SOS_PHYSUSE, YES_REPLY, &msg);
}

L4_Word_t memloc(L4_Word_t addr) {
	L4_Msg_t msg;
	L4_Word_t rvals[2];

	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, addr);
	syscall_generic(vpager(), SOS_MEMLOC, YES_REPLY, rvals, 2, &msg);

	return rvals[1];
}

L4_ThreadId_t vpager(void) {
	static int knowTid = 0;
	static L4_ThreadId_t tid;

	if (!knowTid) {
		L4_Msg_t msg;
		syscall_prepare(&msg);
		tid = L4_GlobalId(syscall(L4_rootserver, SOS_VPAGER, YES_REPLY, &msg), 1);
		knowTid = 1;
	}

	return tid;
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

