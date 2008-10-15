/****************************************************************************
 *
 *      $Id: $
 *
 *      Description: Simple operating system l4 helper functions
 *
 *      Author:		    Godfrey van der Linden
 *
 ****************************************************************************/

#ifndef _LIBSOS_H
#define _LIBSOS_H

#include <stdint.h>
#include <l4/types.h>
#include <l4/thread.h>
#include <bootinfo/bootinfo.h>

#include "constants.h"
#include "pager.h"

#define TAG_SYSLAB(t)	((short) L4_Label(t) >> 4)

#define please(expr)\
	if (!(expr)) {\
		printf("%s line %d failed: ", __FUNCTION__, __LINE__);\
		sos_print_error(L4_ErrorCode());\
	}\

// Clear the message registers.  Useful to use before an L4_Reply()
void msgClearWith(L4_Word_t x);

//
// dprintf(uint32_t verbose_level, const char *msg, ...)
//
// Macro used for debugging your code.  Define a macro 'vervose' within your
// source file then any dprintf request of a lesser level will print.  For
// instance if you #define verbose 2, then all dprintf's of level 0 & 1 will
// print but any of 2 or greater will not print.
//
#define dprintf(v, args...) do { if ((v) < verbose) sos_logf(args); } while (0)

//
// libsos_init()
//
// Function that initialises 'libsos' l4 wrapper.  You must call this very
// early in the startup of your rootserver.  No other functions within libsos
// can be called before init.
//
int libsos_init(void);

//
// sos_find_memory
// 	returns low_memory & high_memory addresses
// 
// Walks the kernel's information page looking for the biggest, contiguous,
// chuck of physical memory that is known to the L4 kernel.  Returns the bounds
// to the caller, NB: the upper bound is exclusive (C standard idiom).  Note
// that the addresses returned are physical and have not yet been mapped into
// the sos address space.
//
void sos_find_memory(L4_Word_t *lowP, L4_Word_t *highP);

// sos_start_binfo_executables
// launches executables listed in the bootinfo
void sos_start_binfo_executables(void);

//
// sos_logf(const char *msg, ...)
// sos_print_fpage(L4_Fpage_t fpage)
// sos_print_error(L4_Word_t ec)
// sos_print_l4memory(void *addr, L4_Word_t len)
//
// Group of functions to output debug messages or to print L4 data structures
// in a easier to read manner.  sos_print_error can only be used for IPC
// errors.  sos_print_l4memory() is a usefull little function that dumps a
// range of memory in reverse, the same way that the L4 manual documents these
// structures.  Useful for debugging protocol messages.
//
void sos_print_fpage(L4_Fpage_t fpage);	// print fpage
void sos_print_error(L4_Word_t ec);	// print l4 error
void sos_print_l4memory(void *addr, L4_Word_t len);
void sos_logf(const char *msg, ...);

//
// sos_get_new_tid(void)
// 	returns an L4_ThreadId_t 
//
// A simple, i.e. broken, kernel thread id allocator.  Just allocates the next
// thread id.  You will probably have to replace this functionality at some
// stage in the future to keep track of currently active threadIds.
//
L4_ThreadId_t sos_get_new_tid(void);

// Disable sos_get_new_tid
void sos_get_new_tid_disable(void);

// get the next thread id that will be issued
L4_ThreadId_t sos_peek_new_tid(void);

//
// sos_my_tid(void)
// Convenience function for accessing a root task thread's id.
// We store the thread id in the user-defined handle in the UTCB.
INLINE L4_ThreadId_t
sos_my_tid(void)
{
    L4_ThreadId_t tid; 
    tid.raw = L4_UserDefinedHandle();
    return tid;
}

//
// sos_thread_new_priority(L4_Word_t prio, void *entry, void *stack)
// sos_thread_new(void *entrypoint, void *stack)
// 	returns an L4_ThreadId_t on success, L4_nilthread otherwise
//
// Create and start a sos thread in the rootserver's protection domain.  The
// sos_thread_new function calls sos_thread_new_priority with a default
// priority.  A new thread is created and started with the given priority,
// initial instruction point(entry) and a pointer to the top of a new stack. 
//
L4_ThreadId_t sos_thread_new_priority(L4_ThreadId_t tid,
		L4_Word_t prio, void *entry, void *stack);
L4_ThreadId_t sos_thread_new(L4_ThreadId_t tid,
		void *entrypoint, void *stack);

//
// sos_task_new(L4_Word_t task, L4_ThreadId_t pager, void *entry, void *stack)
// 	returns an L4_ThreadId_t on success, L4_nilthread otherwise
//
// Create a new protection domain and create a new thread within the protection
// domain with the given entry point and top of stack pointer.  The task
// variable is currently unused, but can be used by you to assign a thread id,
// if you choose.  The pager variable will probably be L4_rootserver.
//
L4_ThreadId_t sos_task_new(L4_Word_t task, L4_ThreadId_t pager,
				  void *entrypoint, void *stack);

#define THREADBITS 8
//
// sos_tid2task(L4_ThreadId_t tid)
// 	returns L4_Word_t
// 
// Function that returns the task associated with the given thread id.  If the
// thread is not a task then 0 is returned.
//
static inline L4_Word_t sos_tid2task(L4_ThreadId_t tid)
{
    return L4_ThreadNo(tid) >> THREADBITS;
}

//
// sos_sid2tid(L4_SpaceId_t sid)
// 	return L4_ThreadId_t
//
// Function that returns the tid associated with the given space.
// This isn't all that safe since it's just based on the assumptions
// that we make regarding sender space with regards to tid.
//
static inline L4_ThreadId_t sos_sid2tid(L4_SpaceId_t sid)
{
	return L4_IsSpaceEqual(sid, L4_rootspace) ?
		pager_get_tid() : L4_GlobalId(L4_SpaceNo(sid), 1);
		//sos_my_tid() : L4_GlobalId(L4_SpaceNo(sid), 1);
}

//
// sos_tid2sid(L4_ThreadNo_t tid)
// 	return L4_SpaceId_t
//
static inline L4_SpaceId_t sos_tid2sid(L4_ThreadId_t tid)
{
	return L4_SpaceId(L4_ThreadNo(tid));
}

//
// getCurrentProcNum(void)
// 	return L4_Word_t
//
// Returns the address space / process number of the current process
// SOS is working on behalf of.
//
// TODO: Should probably just change to using a PCB and this function
// will return a PCB instead of a number
//
L4_Word_t getCurrentProcNum(void);

//
// sos_usleep(uint32_t microseconds)
//
// Put the calling thread to sleep for microseconds.  This function is
// implemented on a temporary L4 debugger api and the timer.c 'timer' driver.
// You will need to reimplment this function when you finish your clock driver.
// NB your timer must work before the network_init() function is called.
//
void sos_usleep(uint32_t microseconds);

// the notify bit that gets set when an interrupt happens
#define SOS_IRQ_NOTIFY_BIT  31

#endif // sos/libsos.h
