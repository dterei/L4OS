/* Simple operating system interface */

#ifndef _LIB_SOS_SOS_H
#define _LIB_SOS_SOS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <l4/message.h>
#include <l4/types.h>

#include <sos/errors.h>

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
        SOS_FLUSH,
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
        SOS_MEMUSE,
        SOS_SWAPUSE,
        SOS_PHYSUSE,
        SOS_VPAGER,
        SOS_MEMLOC,
        SOS_MMAP,
        SOS_SHARE_VM,
        L4_PAGEFAULT = ((L4_Word_t) -2),
        L4_INTERRUPT = ((L4_Word_t) -1),
        L4_EXCEPTION = ((L4_Word_t) -5)
} syscall_t;

/* file modes */
typedef enum {
        FM_READ = 4,
        FM_WRITE = 2,
        FM_EXEC = 1,
        FM_NOTRUNC = 8,
} sos_filemodes;
typedef uint8_t fmode_t;

#define O_RDONLY FM_READ
#define O_WRONLY FM_WRITE
#define O_RDWR   (FM_READ|FM_WRITE)

#define FM_UNLIMITED_RW ((unsigned int) (-1))

/* stat file types */
typedef enum {
        ST_SPECIAL, /* special (console, etc) file */
        ST_FILE, /* plain file */
        ST_DIR, /* directory file */
} st_type_t;

typedef struct {
        st_type_t st_type;  // file type
        fmode_t   st_fmode; // access mode
        size_t    st_size;  // file size in bytes
        /* The fields below for storing the creation and last access time stamp
         * are broken since they are only 4 bytes wide and 8 bytes are needed.
         * However we keep them for compatability with the original sos
         * specification and add two additional correct time stamp fields.
         */
        long st_ctime; // file creation time (ms since booting)
        long st_atime; // file last access (open) time (ms since booting)
        
        /* The correct 8 byte wide unsigned time stamp fields */
        unsigned long long st2_ctime; // file creation time (ms since booting)
        unsigned long long st2_atime; // file last access (open) time (ms since booting)
} stat_t;

typedef int fildes_t;

/* The FD to which printf() will ultimately write() */
extern fildes_t stdout_fd;
extern fildes_t stdin_fd;

/* Max size of a filename */
#define MAX_FILE_NAME 32

/* Process state modes */
typedef enum {
        PS_STATE_START,
        PS_STATE_ALIVE,
        PS_STATE_WAIT,
        PS_STATE_SLEEP,
        PS_STATE_ZOMBIE,
} process_state_t;

/* Process types */
typedef enum {
	PS_TYPE_PROCESS, /* Single threaded user space process */
	PS_TYPE_ROOTTHREAD, /* SOS kernel thread (sos is multi threaded) */
} process_type_t;

/* Process IPC accept types (blocking or non blocking) */
typedef enum {
	PS_IPC_NONBLOCKING, // Only accept non blocking IPC
	PS_IPC_ALL, // Accept all IPC
	PS_IPC_BLOCKING, // Only accept blocking IPC
} process_ipcfilt_t;

/* Process info struct */
typedef struct {
        pid_t              pid;
        unsigned           size;  // in pages
        unsigned           stime; // start time in msec since booting
        unsigned           ctime; // CPU time accumulated in msec
        char               command[MAX_FILE_NAME]; // Name of executable
        process_state_t    state; // state the process is in (run, wait... ect)
		  process_type_t     ps_type; // type of process
		  process_ipcfilt_t  ipc_accept; // type of ipc process accept (blocking or non blocking).
} process_t;

/* Get a string representation of a syscall */
char *syscall_show(syscall_t syscall);

/* Get a string representation of a process state */
char *process_state_show(process_state_t state);

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

/* Copy in a section of memory to the kernel's buffer in perparation for
 * any system call that requres it.
 */
void copyin(void *data, size_t size, int append);

/* Copy out a section of memory to the kernel's buffer in perparation for
 * any system call that requres it.
 */
void copyout(void *data, size_t size, int append);

/* Open file and return file descriptor, -1 if unsuccessful 
 * (too many open files, console already open for reading).
 * A new file should be created if 'path' does not already exist.
 * A failed attempt to open the console for reading (because it is already
 * open) will result in a context switch to reduce the cost of busy waiting
 * for the console.
 * "path" is file name, "mode" is one of O_RDONLY, O_WRONLY, O_RDWR.
 */
fildes_t open(const char *path, fmode_t mode);

/* A nonblocking version of open which assumes a copyin call has already
 * been made.  Use with caution.
 */
void openNonblocking(const char *path, fmode_t mode);

/* A version of open which allows you to lock the file, specifying the max number of
 * times the file can be opened for reading/writing. Can only lock files which haven't
 * been opened yet. So the fiilile should only be opened using open_lock once and
 * subsequent opens should use plain open.
 */
fildes_t open_lock(const char *path, fmode_t mode, unsigned int readers,
                unsigned int writers);

/* A nonblocking version of open_lock
 */
void open_lockNonblocking(const char *path, fmode_t mode, unsigned int readers,
                unsigned int writers);

/* Closes an open file. Returns 0 if successful, -1 if not (invalid "file").
 */
int close(fildes_t file);

/* Nonblocking version of close */
void closeNonblocking(fildes_t file);

/* Read from an open file, into "buf", max "nbyte" bytes.
 * Returns the number of bytes read.
 * Will block when reading from console and no input is presently
 * available. Returns -1 on error (invalid file).
 */
int read(fildes_t file, char *buf, size_t nbyte);

/* A nonblocking version of read which assumes a copyout call will later
 * be made.  Use with caution.
 */
void readNonblocking(fildes_t file, size_t nbyte);

/* Write to an open file, from "buf", max "nbyte" bytes.
 * Returns the number of bytes written. <nbyte disk is full.
 * Returns -1 on error (invalid file).
 */
int write(fildes_t file, const char *buf, size_t nbyte);

/* A nonblocking version of write which assumes a copyin call has already
 * been made.  Use with caution.
 */
void writeNonblocking(fildes_t file, size_t nbyte);

/* Flush a file or stream out to disk/network
 */
int flush(fildes_t file);

/* Nonblocking version of flush
 */
void flushNonblocking(fildes_t file);

/* Lseek sets the file position indicator to the specified position "pos".
 * if "whence" is set to SEEK_SET, SEEK_CUR, or SEEK_END the offset is relative
 * to the start of the file, current position in the file or end of the file
 * respectively.
 *
 * Returns 0 on success and -1 on error.
 */
int lseek(fildes_t file, fpos_t pos, int whence);

/* Nonblocking version of lseek, use with caution.
 */
void lseekNonblocking(fildes_t file, fpos_t pos, int whence); 

/* Reads name of entry "pos" in directory into "name", max "nbyte" bytes.
 * Returns number of bytes returned, zero if "pos" is next free entry,
 * -1 if error (non-existent entry).
 */
int getdirent(int pos, char *name, size_t nbyte);

/* Returns information about file "path" through "buf".
 * Returns 0 if successful, -1 otherwise (invalid name).
 */
int stat(const char *path, stat_t *buf);

/* Nonblocking version of stat.  Must have done copyin already, and must
 * do copyout afterwards.  Note complication with word alignment.
 */
void statNonblocking(void);

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
uint64_t uptime(void);

/* Sleeps for the specified number of microseconds.
 */
void usleep(int usec);

/* Sleeps for the specified number of milliseconds.
 */
void sleep(int msec);

/* Get the number of frames in use by user processes */
int memuse(void);

/* Get the number of pages in use by the swap file */
int swapuse(void);

/* Get the total number of physical frames in use */
int physuse(void);

/* Look up the process' page table for a given virtual address */
L4_Word_t memloc(L4_Word_t addr);

/* Get the threadid of the virtual pager */
L4_ThreadId_t vpager(void);

/*************************************************************************/
/*                                                                       */
/* Optional (bonus) system calls                                         */
/*                                                                       */
/*************************************************************************/

/*
 * Map an area of memory to a file.  This is slightly different from Linux.
 *
 * 	* addr, the address in caller's address space to map to.  Must be either
 * 	  page aligned or left NULL such that the kernel does it automatically.
 *    * size, the size in memory to map.  This can be larger than a page,
 *      or not a multiple, however as mentioned before this only maps in pages.
 *    * rights, the rights of the section.  This will fail if not supported
 *      by the file being mapped to.
 *    * path, the path to the file.  Note that unlike Linux this is not a file
 *      descriptor, in order to support lazy loading of the mapping.  Easily.
 *    * offset, the offset in to the file.  Must be a multiple of pagesize.
 *
 * Also note that any extra room in the page is zeroed.  And that writing is
 * not yet supported (only reading).
 *
 * Returns the address in the caller's address space, or NULL on failure.
 */
void *mmap(void *addr, size_t size, fmode_t rights, char *path, off_t offset);

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
int share_vm(void *adr, size_t size, int writable);

#endif
