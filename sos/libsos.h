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

int init(const bi_user_data_t * data);
int cleanup(const bi_user_data_t * data);
int init_mem(uintptr_t virt_base, uintptr_t virt_end,
		uintptr_t phys_base, uintptr_t phys_end,
		const bi_user_data_t * data);
int init_mem2(uintptr_t virt_base, uintptr_t virt_end,
		uintptr_t phys_base, uintptr_t phys_end,
		const bi_user_data_t * data);
bi_name_t new_pd(bi_name_t owner, const bi_user_data_t * data);
bi_name_t new_ms(bi_name_t owner, uintptr_t base, uintptr_t size,
		uintptr_t flags, uintptr_t attr, bi_name_t physpool,
		bi_name_t virtpool, bi_name_t zone, const bi_user_data_t * data);
int add_virt_mem(bi_name_t pool, uintptr_t base, uintptr_t end,
		const bi_user_data_t * data);
int add_phys_mem(bi_name_t pool, uintptr_t base, uintptr_t end,
		const bi_user_data_t * data);
bi_name_t new_thread(bi_name_t owner, uintptr_t ip, uintptr_t user_main,
		int priority, char * name, size_t name_len,
		const bi_user_data_t * data);
int run_thread(bi_name_t thread, const bi_user_data_t * data);
int map(uintptr_t vaddr, uintptr_t size, uintptr_t paddr,
		int scrub, unsigned mode, const bi_user_data_t * data);
int attach(bi_name_t pd, bi_name_t ms, int rights,
		const bi_user_data_t * data);
int grant(bi_name_t pd, bi_name_t obj, int rights,
		const bi_user_data_t * data);
int argv(bi_name_t thread, char * arg, size_t arg_len,
		const bi_user_data_t * data);
int register_server(bi_name_t thread, bi_name_t ms,
		const bi_user_data_t * data);
int register_callback(bi_name_t pd, bi_name_t ms,
		const bi_user_data_t * data);
int register_stack(bi_name_t thread, bi_name_t ms,
		const bi_user_data_t * data);
bi_name_t new_cap(bi_name_t obj, bi_cap_rights_t rights,
		const bi_user_data_t * data);
int grant_cap(bi_name_t pd, bi_name_t cap, const bi_user_data_t* data);
int export_object(bi_name_t pd, bi_name_t obj,
		bi_export_type_t export_type, char * key, size_t key_len,
		const bi_user_data_t * data);
int export_struct(bi_name_t pd, bi_export_type_t export_type,
		char * key, size_t key_len, uintptr_t first, intptr_t second,
		uintptr_t third, uintptr_t fourth, uintptr_t fifth, uintptr_t sixth,
		const bi_user_data_t * data);
int register_env(bi_name_t pd, bi_name_t ms,
		const bi_user_data_t * data);
bi_name_t ew_pool(int is_virtual, const bi_user_data_t * data);
int grant_interrupt(bi_name_t thread, word_t interrupt,
		const bi_user_data_t * data);
int security_control(bi_name_t pd, bi_name_t obj,
		bi_security_control_type_t security_type,
		const bi_user_data_t * data);
bi_name_t new_zone(bi_name_t pool, const bi_user_data_t * data);
int add_zone_window(bi_name_t pool, size_t base,
		const bi_user_data_t * data);
int kernel_info(int max_spaces, int max_mutexes, int max_root_caps,
		const bi_user_data_t * data);
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
extern int libsos_init(void);

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
extern void sos_find_memory(L4_Word_t *lowP, L4_Word_t *highP);

// sos_start_binfo_executables
// launches executables listed in the bootinfo
extern void sos_start_binfo_executables(void *userstack);

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
extern void sos_print_fpage(L4_Fpage_t fpage);	// print fpage
extern void sos_print_error(L4_Word_t ec);	// print l4 error
extern void sos_print_l4memory(void *addr, L4_Word_t len);
extern void sos_logf(const char *msg, ...);

//
// sos_get_new_tid(void)
// 	returns an L4_ThreadId_t 
//
// A simple, i.e. broken, kernel thread id allocator.  Just allocates the next
// thread id.  You will probably have to replace this functionality at some
// stage in the future to keep track of currently active threadIds.
//
extern L4_ThreadId_t sos_get_new_tid(void);

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
extern L4_ThreadId_t sos_thread_new_priority(L4_Word_t prio,
					   void *entry, void *stack);
extern L4_ThreadId_t sos_thread_new(void *entrypoint, void *stack);

//
// sos_task_new(L4_Word_t task, L4_ThreadId_t pager, void *entry, void *stack)
// 	returns an L4_ThreadId_t on success, L4_nilthread otherwise
//
// Create a new protection domain and create a new thread within the protection
// domain with the given entry point and top of stack pointer.  The task
// variable is currently unused, but can be used by you to assign a thread id,
// if you choose.  The pager variable will probably be L4_rootserver.
//
extern L4_ThreadId_t sos_task_new(L4_Word_t task, L4_ThreadId_t pager,
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
// sos_usleep(uint32_t microseconds)
//
// Put the calling thread to sleep for microseconds.  This function is
// implemented on a temporary L4 debugger api and the timer.c 'timer' driver.
// You will need to reimplment this function when you finish your clock driver.
// NB your timer must work before the network_init() function is called.
//
extern void sos_usleep(uint32_t microseconds);

// XXX Hack: should look this up in USER_HW_VALID_PGSIZES
#define PAGESIZE 4096

// the notify bit that gets set when an interrupt happens
#define SOS_IRQ_NOTIFY_BIT  31

#endif // _LIBSOS_H

