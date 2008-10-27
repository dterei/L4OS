#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <l4/ipc.h>
#include <l4/message.h>
#include <l4/types.h>

#include <sos/sos.h>
#include <sos/ipc.h>

fildes_t stdout_fd = 0;
fildes_t stderr_fd = 1;
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
		case SOS_MMAP: return "SOS_MMAP";
		case SOS_SHARE_VM: return "SOS_SHARE_VM";
		case L4_PAGEFAULT: return "L4_PAGEFAULT";
		case L4_INTERRUPT: return "L4_INTERRUPT";
		case L4_EXCEPTION: return "L4_EXCEPTION";
	}

	return "UNRECOGNISED";
}

char *process_state_show(process_state_t state) {
	switch (state) {
		case PS_STATE_START: return "START";
		case PS_STATE_ALIVE: return "RUN";
		case PS_STATE_WAIT: return "WAIT";
		case PS_STATE_SLEEP: return "SLEEP";
		case PS_STATE_ZOMBIE: return "ZOMBIE";
	}

	return "INVALID";
}

void kprint(char *str) {
	copyin(str, strlen(str) + 1, 0);

	ipc_send_simple_0(L4_rootserver, SOS_KERNEL_PRINT, NO_REPLY);
}

void debug_flush(void) {
	ipc_send_simple_0(vpager(), SOS_DEBUG_FLUSH, NO_REPLY);
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
	int rval = ipc_send_simple_1(vpager(), SOS_MOREMEM, YES_REPLY, nb);

	if (rval == 0) {
		return 0; // no memory
	} else {
		copyout(base, sizeof(uintptr_t), 0);
		return rval;
	}
}

void copyin(void *data, size_t size, int append) {
	ipc_send_simple_3(vpager(), SOS_COPYIN, YES_REPLY, (L4_Word_t) data,
			(L4_Word_t) size, (L4_Word_t) append);
}

void copyout(void *data, size_t size, int append) {
	ipc_send_simple_3(vpager(), SOS_COPYOUT, YES_REPLY, (L4_Word_t) data,
			(L4_Word_t) size, (L4_Word_t) append);
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

	return ipc_send_simple_3(L4_rootserver, SOS_OPEN, YES_REPLY,
			(L4_Word_t) mode, (L4_Word_t) readers, (L4_Word_t) writers);
}

void open_lockNonblocking(const char *path, fmode_t mode, unsigned int readers,
		unsigned int writers) {
	if (path != NULL) {
		copyin((void*) path, strlen(path) + 1, 0);
	}

	ipc_send_simple_3(L4_rootserver, SOS_OPEN, NO_REPLY, (L4_Word_t) mode,
			(L4_Word_t) readers, (L4_Word_t) writers);
}

int close(fildes_t file) {
	return ipc_send_simple_1(L4_rootserver, SOS_CLOSE, YES_REPLY, (L4_Word_t) file);
}

void closeNonblocking(fildes_t file) {
	ipc_send_simple_1(L4_rootserver, SOS_CLOSE, NO_REPLY, (L4_Word_t) file);
}

int read(fildes_t file, char *buf, size_t nbyte) {
	// flush stdout
	flush(stdout_fd);

	int rval = ipc_send_simple_2(L4_rootserver, SOS_READ, YES_REPLY,
			(L4_Word_t) file, (L4_Word_t) nbyte);

	copyout(buf, nbyte, 0);

	return rval;
}

void readNonblocking(fildes_t file, size_t nbyte) {
	ipc_send_simple_2(L4_rootserver, SOS_READ, NO_REPLY, (L4_Word_t) file,
			(L4_Word_t) nbyte);
}

int write(fildes_t file, const char *buf, size_t nbyte) {
	copyin((void*) buf, nbyte, 0);

	return ipc_send_simple_2(L4_rootserver, SOS_WRITE, YES_REPLY,
			(L4_Word_t) file, (L4_Word_t) nbyte);
}

void writeNonblocking(fildes_t file, size_t nbyte) {
	ipc_send_simple_2(L4_rootserver, SOS_WRITE, NO_REPLY,
			(L4_Word_t) file, (L4_Word_t) nbyte);
}

/* Flush a file or stream out to disk/network */
int flush(fildes_t file) {
	return ipc_send_simple_1(L4_rootserver, SOS_FLUSH, YES_REPLY,
			(L4_Word_t) file);
}

/* Flush a file or stream out to disk/network */
void flushNonblocking(fildes_t file) {
	ipc_send_simple_1(L4_rootserver, SOS_FLUSH, NO_REPLY,
			(L4_Word_t) file);
}

/* Lseek sets the file position indicator to the specified position "pos".
 * if "whence" is set to SEEK_SET, SEEK_CUR, or SEEK_END the offset is relative
 * to the start of the file, current position in the file or end of the file
 * respectively.
 *
 * Returns 0 on success and -1 on error.
 */
int lseek(fildes_t file, fpos_t pos, int whence) {
	return ipc_send_simple_3(L4_rootserver, SOS_LSEEK, YES_REPLY,
			(L4_Word_t) file, (L4_Word_t) pos, (L4_Word_t) whence);
}

void lseekNonblocking(fildes_t file, fpos_t pos, int whence) {
	ipc_send_simple_3(L4_rootserver, SOS_LSEEK, NO_REPLY,
			(L4_Word_t) file, (L4_Word_t) pos, (L4_Word_t) whence);
}

/* 
 * Reads name of entry "pos" in directory into "name", max "nbyte" bytes.
 * Returns number of bytes returned, zero if "pos" is next free entry,
 * -1 if error (non-existent entry).
 */
int getdirent(int pos, char *name, size_t nbyte) {
	int rval;

	rval = ipc_send_simple_2(L4_rootserver, SOS_GETDIRENT, YES_REPLY,
			(L4_Word_t) pos, (L4_Word_t) nbyte);

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

	int rval = ipc_send_simple_0(L4_rootserver, SOS_STAT, YES_REPLY);

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

void statNonblocking(void) {
	ipc_send_simple_0(L4_rootserver, SOS_STAT, NO_REPLY);
}

/* Duplicate an open file handler to given a second file handler which points
 * to the same open file. The two file handlers point to the same open file
 * and so share the same offset pointer and open mode.
 */

/* This method returns the first free file descriptor slot found as the duplicate */
fildes_t dup(fildes_t file) {
	return dup2(file, VFS_NIL_FILE);
}

/* This method duplicate file to a file descriptor newfile. If newfile is already in
 * use then it is closed.
 */
fildes_t dup2(fildes_t file, fildes_t newfile) {
	return ipc_send_simple_2(L4_rootserver, SOS_DUP, YES_REPLY, file, newfile);
}

/* Removees the specified file "path".
 * Returns - if successful, -1 otherwise (invalid name).
 */
int fremove(const char *path) {
	int len = strlen(path);
	copyin((void*) path, len + 1, 0);

	return ipc_send_simple_0(L4_rootserver, SOS_REMOVE, YES_REPLY);
}

/* 
 * Create a new process running the executable image "path".
 * Returns ID of new process, -1 if error (non-executable image, nonexisting
 * file).
 */
pid_t process_create(const char *path) {
	return process_create2(path, VFS_NIL_FILE, VFS_NIL_FILE, VFS_NIL_FILE);
}

/* Create a new process running the executable image "path".
 *
 * Sets the new processes stdout, stderr and stdin to the file descriptors
 * specified. A value of VFS_NIL_FILE for any of the file descriptors sets
 * the system default to be used.
 * 
 * Returns ID of new process, -1 if error (non-executable image, nonexisting
 * file).
 */
pid_t process_create2(const char *path, fildes_t fdout, fildes_t fderr, fildes_t fdin) {
	copyin((void*) path, strlen(path) + 1, 0);

	return (pid_t) ipc_send_simple_3(vpager(), SOS_PROCESS_CREATE, YES_REPLY, fdout, fderr, fdin);
}

/* 
 * Delete process (and close all its file descriptors).
 * Returns 0 if successful, -1 otherwise (invalid process).
 */
int process_delete(pid_t pid) {
	return ipc_send_simple_1(vpager(), SOS_PROCESS_DELETE, YES_REPLY,
			(L4_Word_t) pid);
}

/* Returns ID of caller's process. */
pid_t my_id(void) {
	return ipc_send_simple_0(L4_rootserver, SOS_MY_ID, YES_REPLY);
}

/* 
 * Returns through "processes" status of active processes (at most "max"),
 * returns number of process descriptors actually returned.
 */
int process_status(process_t *processes, unsigned max) {
	int rval = ipc_send_simple_1(vpager(), SOS_PROCESS_STATUS, YES_REPLY,
			(L4_Word_t) max);

	copyout(processes, rval * sizeof(process_t), 0);

	return rval;
}

/* 
 * Wait for process "pid" to exit. If "pid" is -1, wait for any process
 * to exit. Returns the pid of the process which exited.
 */
pid_t process_wait(pid_t pid) {
	return ipc_send_simple_1(vpager(), SOS_PROCESS_WAIT, YES_REPLY,
			(L4_Word_t) pid);
}

/* Returns time in microseconds since booting. */
uint64_t uptime(void) {
	L4_Word_t rtime[2];

	ipc_send(L4_rootserver, SOS_TIME_STAMP, YES_REPLY, 2, rtime, 0);

	uint64_t t = rtime[1];
	t = t << 32;
	t += rtime[0];
	return t;
}

/* Sleeps for the specified number of microseconds. */
void usleep(int usec) {
	ipc_send_simple_1(L4_rootserver, SOS_USLEEP, YES_REPLY, (L4_Word_t) usec);
}

/* Get the number of frames in use by user processes */
int memuse(void) {
	return ipc_send_simple_0(vpager(), SOS_MEMUSE, YES_REPLY);
}

int swapuse(void) {
	return ipc_send_simple_0(vpager(), SOS_SWAPUSE, YES_REPLY);
}

int physuse(void) {
	return ipc_send_simple_0(vpager(), SOS_PHYSUSE, YES_REPLY);
}

L4_Word_t memloc(L4_Word_t addr) {
	return ipc_send_simple_0(vpager(), SOS_MEMLOC, YES_REPLY);
}

L4_ThreadId_t vpager(void) {
	L4_Word_t id = ipc_send_simple_0(L4_rootserver, SOS_VPAGER, YES_REPLY);
	return L4_GlobalId(id, 1);
}

void *mmap(void *addr, size_t size, fmode_t rights, char *path, off_t offset) {
	copyin(path, strlen(path) + 1, 0);
	return (void*) ipc_send_simple_4(vpager(), SOS_MMAP, YES_REPLY,
			(L4_Word_t) addr, size, rights, offset);
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

