#ifndef _PROCESS_H_
#define _PROCESS_H_

#include "pager.h"

#define RUN_AS_PROCESS 0
#define RUN_AS_THREAD 1

// The process data structure
typedef struct Process_t Process;

// Find a process
Process *process_lookup(L4_Word_t key);

// Create a new process with a given name
Process *process_init(void);

// Add a region to a process
void process_add_region(Process *p, Region *r);

// Set the name of the process
void process_set_name(Process *p, char *name);

// Set an initial stack pointer for the process
void process_set_sp(Process *p, void *sp);

// Set an initial instruction pointer for the process
void process_set_ip(Process *p, void *ip);

// Prepare a process to be run
void process_prepare(Process *p);

// Run a process
L4_ThreadId_t process_run(Process *p, int asThread);

// Get the process id of a process
pid_t process_get_pid(Process *p);

// Get the threadid of a process
L4_ThreadId_t process_get_tid(Process *p);

// Get the page table of a process
PageTable *process_get_pagetable(Process *p);

// Get the regions of a process
Region *process_get_regions(Process *p);

// Set the pager request associated with a process
void process_set_prequest(Process *p, PagerRequest *pr);

// Get the pager request associated with a process
PagerRequest *process_get_prequest(Process *p);

// Kill a process and free its resources
void process_kill(pid_t pid);

// Write the status of the first n processes to dest
int process_write_status(process_t *dest, int n);

#endif // process.h
