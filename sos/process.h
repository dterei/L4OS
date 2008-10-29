#ifndef _PROCESS_H_
#define _PROCESS_H_

#include <sos/sos.h>

#include "list.h"
#include "region.h"
#include "pager.h"
#include "vfs.h"

#define NIL_PID (pid_t) (-1)

#define YES_TIMESTAMP 1
#define NO_TIMESTAMP 0

// The process data structure
typedef struct Process_t Process;

// Get and reserve a pid for future use
pid_t reserve_pid(void);

// Find a process from a thread
Process *process_lookup(pid_t key);

// Create a new process or root thread
Process *process_init(process_type_t type);

// Add a region to a process
void process_add_region(Process *p, Region *r);

// Append a region to a process of a certain size and rights
L4_Word_t process_append_region(Process *p, size_t size, int rights);

// Set the name of the process
void process_set_name(Process *p, const char *name);

// Set the state of the process
void process_set_state(Process *p, process_state_t state);

// Set the ipc types the process accepts (return the old one)
process_ipcfilt_t process_set_ipcfilt(Process *p, process_ipcfilt_t ipc_filt);

// Set an initial stack pointer for the process
void process_set_sp(Process *p, void *sp);

// Set an initial instruction pointer for the process
void process_set_ip(Process *p, void *ip);

// Prepare a process to be run
void process_prepare(Process *p);

// Prepare a process to be run, sets the new processes stdfds to the fds specified
// of the parent process
void process_prepare2(Process *p, char* fdout, char* fderr, char* fdin);

// Start a new root thread (Stack size of a page).
Process *process_run_rootthread(const char *name, void *ip, int ts, int prio);

// Run a process
L4_ThreadId_t process_run(Process *p, int timestamp);

// Get the state of the process
process_state_t process_get_state(Process *p);

// Get the ipc accept flag
process_ipcfilt_t process_get_ipcfilt(Process *p);

// Get the process id of a process
pid_t process_get_pid(Process *p);

// Get the threadid of a process
L4_ThreadId_t process_get_tid(Process *p);

// Get the spaceid of a process
L4_SpaceId_t process_get_sid(Process *p);

// Get the stdin redirectin setting of a process
char *process_get_stdin(Process *p);

// Set the stdin redirectin setting of a process
char *process_set_stdin(Process *p, char *in);

// Get the page table of a process
Pagetable *process_get_pagetable(Process *p);

// Get the regions of a process
List *process_get_regions(Process *p);

// Wake all processes waiting on a process
void process_wake_all(pid_t pid);

// Close all files opened by a process
void process_close_files(Process *p);

// Kill a thread
L4_Word_t thread_kill(L4_ThreadId_t tid);

// Are we allowed to kill a process?
int process_can_kill(Process *p);

// Kill a process, assumes we are allowed to
void process_kill(Process *p);

// Remove process PCB from list
void process_remove(Process *p);

// Write the status of the first n processes to dest
int process_write_status(process_t *dest, int n);

// Get a pointer to a process_t struct from the process
process_t *process_get_info(Process *p);

// Get the list of files used by a process
VFile *process_get_ofiles(Process *p);

// Get the list of open inodes used by a process
fildes_t *process_get_fds(Process *p);

// Wait for any process to exit before waking
void process_wait_any(Process *waiter);

// Wait for a specified process to exit before waking
void process_wait_for(Process *waitFor, Process *waiter);

// Give the rootserver a PCB
void process_add_rootserver(void);

// Debug print a process
void process_dump(Process *p);

#endif // sos/process.h
