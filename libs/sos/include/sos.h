/* Simple operating system interface */

#ifndef _SOS_H
#define _SOS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <l4/message.h>
#include <l4/types.h>

/* VFS Return Codes */
typedef enum {
	SOS_VFS_OK = 0,
	SOS_VFS_EOF = 0,
	SOS_VFS_ERROR = -1,
	SOS_VFS_PERM = -2,
	SOS_VFS_NOFILE = -3,
	SOS_VFS_NOVNODE = -4,
	SOS_VFS_NOMEM = -5,
	SOS_VFS_NOMORE = -6,
	SOS_VFS_PATHINV = -7,
	SOS_VFS_CORVNODE = -8,
	SOS_VFS_NOTIMP = -9
} vfs_return_t;

/* System calls for SOS */
typedef enum {
	SOS_REPLY,
	SOS_KERNEL_PRINT,
	SOS_DEBUG_FLUSH,
	SOS_MOREMEM,
	SOS_COPYIN,
	SOS_COPYOUT,
	SOS_OPEN,
	SOS_CLOSE,
	SOS_READ,
	SOS_WRITE,
	SOS_LSEEK,
	SOS_GETDIRENT,
	SOS_STAT,
	SOS_REMOVE,
	SOS_FLUSH,
	SOS_PROCESS_CREATE,
	SOS_PROCESS_DELETE,
	SOS_MY_ID,
	SOS_PROCESS_STATUS,
	SOS_PROCESS_WAIT,
	SOS_PROCESS_NOTIFY_ALL,
	SOS_TIME_STAMP,
	SOS_USLEEP,
	SOS_MEMUSE,
	SOS_SWAPUSE,
	SOS_VPAGER,
	SOS_MEMLOC,
	SOS_SHARE_VM,
	L4_PAGEFAULT = ((L4_Word_t) -2),
	L4_INTERRUPT = ((L4_Word_t) -1),
	L4_EXCEPTION = ((L4_Word_t) -5)
} syscall_t;

/* file modes */
#define FM_WRITE 1
#define FM_READ  2
#define FM_EXEC  4
typedef uint8_t fmode_t;

#define O_RDONLY FM_READ
#define O_WRONLY FM_WRITE
#define O_RDWR   (FM_READ|FM_WRITE)

/* stat file types */
typedef enum {
	ST_FILE, /* plain file */
	ST_SPECIAL, /* special (console, etc) file */
} st_type_t;

typedef struct {
	st_type_t st_type;  // file type
	fmode_t   st_fmode; // access mode
	size_t    st_size;  // file size in bytes
	long	   st_ctime; // file creation time (ms since booting)
	long	   st_atime; // file last access (open) time (ms since booting)
} stat_t;

typedef int fildes_t;

/* The FD to which printf() will ultimately write() */
extern fildes_t stdout_fd;
extern fildes_t stdin_fd;

/* Max size of a filename */
#define N_NAME 32

typedef struct {
	pid_t     pid;
	unsigned  size;  // in pages
	unsigned  stime; // start time in msec since booting
	unsigned  ctime; // CPU time accumulated in msec
	char	    command[N_NAME]; // Name of exectuable
} process_t;

/* Get the string representation of a syscall */
char *syscall_show(syscall_t syscall);

/* For handling syscalls */
#define YES_REPLY 1
#define NO_REPLY 0

/* Prepare for a syscall to be made */
void syscall_prepare(L4_Msg_t *msg);

/* Make a syscall, with nRvals return value placed in rvals */
void syscall_generic(L4_ThreadId_t tid, syscall_t s, int reply,
		L4_Word_t *rvals, int nRvals, L4_Msg_t *msg);

/* An interface to syscall_generic that returns a single value */
L4_Word_t syscall(L4_ThreadId_t, syscall_t s, int reply, L4_Msg_t *msg);

/* Misc system calls */

/* Print something in the kernel */
void kprint(char *str);

/* Flush L4's page table. */
void debug_flush(void);

/* Block the calling thread. */
void thread_block(void);

/* Request more memory for the heap section. */
int moremem(uintptr_t *base, unsigned int nb);

/* I/O system calls */

/*
 * Copy in a section of memory to the kernel's buffer in perparation for
 * any system call that requres it.
 */
void copyin(void *data, size_t size, int append);

/*
 * Copy out a section of memory to the kernel's buffer in perparation for
 * any system call that requres it.
 */
void copyout(void *data, size_t size, int append);

/*
 * Open file and return file descriptor, -1 if unsuccessful 
 * (too many open files, console already open for reading).
 * A new file should be created if 'path' does not already exist.
 * A failed attempt to open the console for reading (because it is already
 * open) will result in a context switch to reduce the cost of busy waiting
 * for the console.
 * "path" is file name, "mode" is one of O_RDONLY, O_WRONLY, O_RDWR.
 */
fildes_t open(const char *path, fmode_t mode);

/* Closes an open file. Returns 0 if successful, -1 if not (invalid "file").
 */
int close(fildes_t file);

/* Read from an open file, into "buf", max "nbyte" bytes.
 * Returns the number of bytes read.
 * Will block when reading from console and no input is presently
 * available. Returns -1 on error (invalid file).
 */
int read(fildes_t file, char *buf, size_t nbyte);

/* Write to an open file, from "buf", max "nbyte" bytes.
 * Returns the number of bytes written. <nbyte disk is full.
 * Returns -1 on error (invalid file).
 */
int write(fildes_t file, const char *buf, size_t nbyte);

/* Lseek sets the file position indicator to the specified position "pos".
 * if "whence" is set to SEEK_SET, SEEK_CUR, or SEEK_END the offset is relative
 * to the start of the file, current position in the file or end of the file
 * respectively.
 *
 * Note: SEEK_END not supported.
 *
 * Returns 0 on success and -1 on error.
 */
int lseek(fildes_t file, fpos_t pos, int whence);

/* Reads name of entry "pos" in directory into "name", max "nbyte" bytes.
 * Returns number of bytes returned, zero if "pos" is next free entry,
 * -1 if error (non-existent entry).
 */
int getdirent(int pos, char *name, size_t nbyte);

/* Returns information about file "path" through "buf".
 * Returns 0 if successful, -1 otherwise (invalid name).
 */
int stat(const char *path, stat_t *buf);

/* Removees the specified file "path".
 * Returns - if successful, -1 otherwise (invalid name).
 */
int fremove(const char *path);

/*
 * Flush stdout
 */
void flush(void);

/* Create a new process running the executable image "path".
 * Returns ID of new process, -1 if error (non-executable image, nonexisting
 * file).
 */
pid_t process_create(const char *path);

/* Delete process (and close all its file descriptors).
 * Returns 0 if successful, -1 otherwise (invalid process).
 */
int process_delete(pid_t pid);

/* Returns ID of caller's process. */
pid_t my_id(void);

/* Returns through "processes" status of active processes (at most "max"),
 * returns number of process descriptors actually returned.
 */
int process_status(process_t *processes, unsigned max);

/* Wait for process "pid" to exit. If "pid" is -1, wait for any process
 * to exit. Returns the pid of the process which exited.
 */
pid_t process_wait(pid_t pid);

/*
 * Wake all processes waiting on a certain pid (those that called
 * process_wait with a relevant value.
 * Note that only privileged threads can call this.
 * Returns 0 on success, anything else on failure.
 */
int process_notify_all(pid_t pid);

/* Returns time in microseconds since booting.
 */
uint64_t uptime(void);

/* Sleeps for the specified number of microseconds.
 */
void usleep(int msec);

/* Get the number of frames in use by user processes */
int memuse(void);

/* Get the number of pages in use by the swap file */
int swapuse(void);

/* Look up the process' page table for a given virtual address */
L4_Word_t memloc(L4_Word_t addr);

/* Get the threadid of the virtual pager */
L4_ThreadId_t vpager(void);

/*************************************************************************/
/*									 */
/* Optional (bonus) system calls					 */
/*									 */
/*************************************************************************/

int share_vm(void *adr, size_t size, int writable);
/* Make VM region ["adr","adr"+"size") sharable by other processes.
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

#endif
