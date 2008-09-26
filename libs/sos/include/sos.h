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
	SOS_PROCESS_CREATE,
	SOS_PROCESS_DELETE,
	SOS_MY_ID,
	SOS_PROCESS_STATUS,
	SOS_PROCESS_WAIT,
	SOS_TIME_STAMP,
	SOS_USLEEP,
	SOS_SHARE_VM
} syscall_t;

/* Limits */
#define MAX_ADDRSPACES 256
#define MAX_THREADS 4096
#define PROCESS_MAX_FILES 16
#define MAX_IO_BUF 1024
#define N_NAME 32

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

void prepare_syscall(L4_Msg_t *msg);
L4_Word_t make_syscall(syscall_t s, int reply, L4_Msg_t *msg);

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

/* Returns time in microseconds since booting.
 */
long uptime(void);

/* Sleeps for the specified number of microseconds.
 */
void usleep(int msec);


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
