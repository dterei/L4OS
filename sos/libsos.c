/****************************************************************************
 *
 *      $Id: $
 *
 *      Description: Simple operating system l4 helper functions
 *
 *      Author:		    Godfrey van der Linden
 *      Original Author:    Ben Leslie
 *
 ****************************************************************************/

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <l4/types.h>

#include <l4/config.h>
#include <l4/ipc.h>
#include <l4/thread.h>
#include <l4/schedule.h>
#include <l4/caps.h>

#include <nfs/nfs.h>
#include <serial/serial.h>

#include "libsos.h"
#include "pager.h"

// Hack externs from timer.c
extern void utimer_init(void);
extern void utimer_sleep(uint32_t microseconds);

#define verbose 1

#define ONE_MEG	    (1 * 1024 * 1024)

extern void _start(void);

static L4_Word_t sSosMemoryTop, sSosMemoryBot;

static L4_Fpage_t utcb_fpage_s;
static L4_Word_t utcb_base_s;
static L4_Word_t last_thread_s;
/* Address of the bootinfo buffer. */
extern void *__okl4_bootinfo;

// That odd (Iguana) way of identifying memory sections.
static unsigned int current_ms = 1;

// Orphaned memory sections (ones that haven't been
// attached to anything)
//typedef struct OrphanListT *OrphanList;
//struct OrphanListT {
//	Region *region;
//	OrphanList next;
//};
//
//OrphanList orphans = NULL;

/* Initialise the L4 Environment library */
int
libsos_init(void)
{
    // Announce ourself to the world
    puts("\n*********************************");

    last_thread_s = L4_ThreadNo(L4_rootserver);

    sos_logf("SOS Starting. My thread id is %lx, next %lx\n\n",
	    L4_ThreadNo(L4_rootserver), last_thread_s+1);
    
    // get my localid to find the utcb base
    utcb_base_s  = (L4_Word_t) L4_GetUtcbBase();    // XXX gvdl: inder or cast?
    assert(!(utcb_base_s & (UTCB_ALIGNMENT - 1)));

    if (!L4_UtcbIsKernelManaged()) {
        utcb_fpage_s.raw = L4_GetUtcbBits();
        utcb_fpage_s = L4_FpageLog2(utcb_base_s, utcb_fpage_s.raw);
        dprintf(0, "UTCB at %lx l2sz %lx\n", utcb_base_s, utcb_fpage_s.raw);
    }
    else {  // No userland utcb map
        utcb_fpage_s = L4_Nilpage;
        utcb_base_s      = -1UL;
        dprintf(0, "UTCB under kernel control\n");
    }

    dprintf(1, "BootInfo at %p\n", __okl4_bootinfo);
    
    /* allow ourself to control each of the interrupts */
    for (int i = 0 ; i < 32 ; i++) {
        L4_LoadMR(0, i);
        L4_Word_t success = L4_AllowInterruptControl(L4_rootspace);
        assert(success);
    }

    utimer_init();

    return 0;
}

/* This is passed the biggest section of physical memory */
static int
bootinfo_init_mem(uintptr_t virt_base, uintptr_t virt_end,
        uintptr_t phys_base, uintptr_t phys_end,
        const bi_user_data_t * data)
{
	sSosMemoryBot = phys_base;
	sSosMemoryTop  = phys_end;
	current_ms += 3;
	return 0;
}

/* Find the largest available chunk of physical RAM */
void
sos_find_memory(L4_Word_t *lowP, L4_Word_t *highP)
{
    int result;
    bi_callbacks_t bi_callbacks = OKL4_BOOTINFO_CALLBACK_INIT;
    
    bi_callbacks.init_mem = bootinfo_init_mem;
    result = bootinfo_parse(__okl4_bootinfo, &bi_callbacks, NULL);
    assert(!result);
    
    dprintf(2, "Biggest conventional memory %lx-%lx\n", sSosMemoryBot, sSosMemoryTop);
    
    *highP = sSosMemoryTop - ONE_MEG;
    *lowP  = sSosMemoryBot;
    
    sSosMemoryBot = sSosMemoryTop - ONE_MEG;
}

//
// L4 Debug output functions
//

// pretty print an L4 error code
void
sos_print_error(L4_Word_t ec)
{
    L4_Word_t e, p;

    /* mmm, seedy */
    e = (ec >> 1) & 15;
    p = ec & 1;

    // XXX gvdl: get page number when doc is available
    sos_logf("IPC %s error, code is 0x%lx (see p125)\n", p? "receive":"send", e);
    if (e == 4)
	sos_logf("  Message overflow error\n");
}

void sos_print_l4memory(void *addr, L4_Word_t len)
{
    len = ((len + 15) & ~15);	// Round up to next 16
    unsigned int *wp = &((unsigned int *) addr)[len / sizeof(unsigned)];
    do {
	wp -= 4;
	sos_logf("%8p: %08x %08x %08x %08x\n", wp, wp[3], wp[2], wp[1], wp[0]);
    } while (wp != addr);

}

// pretty print an L4 fpage
void
sos_print_fpage(L4_Fpage_t fpage)
{
    sos_logf("fpage(%lx - %lx)",
	    L4_Address(fpage), L4_Address(fpage) + L4_Size(fpage));
}

void sos_logf(const char *msg, ...)
{
    va_list alist;

    va_start(alist, msg);
    vprintf(msg, alist);
    va_end(alist);
}


//
// Thread and task ID handling function
//

// A simple, i.e. broken, kernel thread id allocator
L4_ThreadId_t
sos_get_new_tid(void)
{
    dprintf(3, "allocating new tid %lu\n", last_thread_s+1);
    return L4_GlobalId(++last_thread_s, 1);
}

// get the next thread id that will be issued
L4_ThreadId_t
sos_peek_new_tid(void)
{
    return L4_GlobalId(last_thread_s+1, 1);
}

// Create a new thread
static inline L4_ThreadId_t
create_thread(L4_ThreadId_t tid, L4_ThreadId_t scheduler)
{
    L4_Word_t utcb_location = utcb_base_s;
    if (!L4_UtcbIsKernelManaged())
        utcb_location += L4_GetUtcbSize() * L4_ThreadNo(tid);

    // Create active thread
    int res = L4_ThreadControl(tid,
			       L4_rootspace,	// address space
			       scheduler,	// scheduler
			       L4_rootserver,	// pager
			       L4_rootserver,	// exception handler
			       0,	// resources
			       (void *) utcb_location);
    if (!res) {
	sos_logf("ERROR(%lu): ThreadControl(%lx) utcb %lx\n",
		L4_ErrorCode(), tid.raw, utcb_location);
	tid = L4_nilthread;
    }
    
    // put its tid in its UTCB
    L4_Set_UserDefinedHandleOf(tid, tid.raw);
    
    return tid;
}

// Create and start a sos thread in the rootserver
L4_ThreadId_t
sos_thread_new_priority(L4_Word_t prio, void *entry, void *stack)
{
    L4_ThreadId_t tid = sos_get_new_tid();
    L4_ThreadId_t sched = (prio)? L4_rootserver : L4_anythread;

    // This bit creates the thread, but it won't execute any code yet 
    tid = create_thread(tid, sched);
    if (!L4_IsNilThread(tid)) {
	if (prio && !L4_Set_Priority(tid, prio)) {
	    sos_logf("sos: failed to set priority for %lx\n", tid.raw);
	    sos_print_error(L4_ErrorCode());
	}

	// Send an ipc to thread thread to start it up
	L4_Start_SpIp(tid, (L4_Word_t) stack, (L4_Word_t) entry);
    }

    return tid;
}

// Create and start a sos thread in the rootserver
L4_ThreadId_t
sos_thread_new(void *entrypoint, void *stack)
{
    return sos_thread_new_priority(/* prio */ 0, entrypoint, stack);
}

// Create and start a new task
L4_ThreadId_t
sos_task_new(L4_Word_t task, L4_ThreadId_t pager, 
	     void *entrypoint, void *stack)
{
    // HACK: Workaround for compiler bug, volatile qualifier stops the internal
    // compiler error.
    volatile uint32_t taskId = task << THREADBITS;
    L4_SpaceId_t spaceId = L4_SpaceId(task);
    L4_ClistId_t clistId = L4_ClistId(task);
    int res;

    res = L4_CreateClist(clistId, 32); // 32 slots
    if (!res)
        return ((L4_ThreadId_t) { raw : -1});
    
    // Setup space
    res = L4_SpaceControl(spaceId, L4_SpaceCtrl_new, clistId, utcb_fpage_s, 0, NULL);
    if (!res)
        return ((L4_ThreadId_t) { raw : -2});
	
	// Give the space a cap to the root server
	res = L4_CreateIpcCap(L4_rootserver, L4_rootclist,
	                      L4_rootserver, clistId);
	assert(res);

    // Create the thread
    L4_ThreadId_t tid = L4_GlobalId(taskId, 1);
    res = L4_ThreadControl(tid, spaceId, L4_rootserver,
	    pager, pager, 0, (void *) utcb_base_s);
    if (!res)
        return ((L4_ThreadId_t) { raw : -3});

    L4_Start_SpIp(tid, (L4_Word_t) stack, (L4_Word_t) entrypoint);

    return tid;
}

bi_name_t
bootinfo_new_ms(bi_name_t owner, uintptr_t base, uintptr_t size,
		uintptr_t flags, uintptr_t attr, bi_name_t physpool,
		bi_name_t virtpool, bi_name_t zone, const bi_user_data_t * data)
{
	dprintf(0, "*** new_ms: %u, %u, %u, %u, %u, %u (%d, %u)\n", owner, base, size,
			flags, attr, data->rec_num, last_thread_s, current_ms);
	
	// ignore if root task
	if (owner == 0)
		return 0;


	// need to translate owner to an address space id. Should we be storing with
	// each address space an int with the owner in it?
	// then just malloc a new region and add it to the list

	// create new region
	Region *newreg = (Region *)malloc(sizeof(Region));
	newreg->pbase = base;
	newreg->psize = size;
	newreg->vbase = 0;
	newreg->vsize = 0;
	newreg->rights = 0;
	newreg->ms = current_ms;
	newreg->next = NULL;

	// add to the list of orphans (because it hasn't been attached yet).
	//OrphanList newOrphan = (OrphanList) malloc(sizeof(struct OrphanListT));
	//newOrphan->next = orphans;
	//newOrphan->region = newreg;
	//orphans = newOrphan;

	// add to address space
	// Assume the address space to attach to is the last one just created since
	// that is the usual bootloader sequence. But we check the owner variables
	// match and if they dont we then do a simple linear search.
	
	L4_Word_t as = last_thread_s;
	if (addrspace[as].pd != owner)
	{
		as = 0;
		for (int i = 0; i < MAX_ADDRSPACES; i++)
		{
			if (addrspace[i].pd == owner)
			{
				as = i;
				break;
			}
		}
	}
	
	if (as == 0)
	{
		return BI_NAME_INVALID;
	}

	
	Region *rspot = NULL;
	if (addrspace[as].regions == NULL)
	{
		addrspace[as].regions = newreg;
	}
	else
	{
		while ((rspot = rspot->next) != NULL)
			;
		rspot = newreg;
	}

	current_ms++;
	return owner;
}

int
bootinfo_attach(bi_name_t pd, bi_name_t ms, int rights,
		const bi_user_data_t * data)
{
	// how do we determine which memory section they are talking about? data->rec_num
	// is meant to be the same in both attach and new_ms apparently so that we can
	// tell this, but from our testing they aren't the same, should we be setting the
	// data->rec_num?

	// First check already-attached memory regions because this might be an
	// update.

	// If not found then look through the list of orphans.

	// then need to store the rights with the region.
	dprintf(0, "test: attach: %d, %d, %d, %d (%d)\n", pd, ms, rights,
			data->rec_num);

	// ignore if root task
	if (pd == 0)
		return 0;
	

	// add to address space
	// Assume the address space to attach to is the last one just created since
	// that is the usual bootloader sequence. But we check the owner variables
	// match and if they dont we then do a simple linear search.
	
	L4_Word_t as = last_thread_s;
	if (addrspace[as].pd != pd)
	{
		as = 0;
		for (int i = 0; i < MAX_ADDRSPACES; i++)
		{
			if (addrspace[i].pd == pd)
			{
				as = i;
				break;
			}
		}
	}
	
	if (as == 0 || addrspace[as].regions == NULL)
	{
		return BI_NAME_INVALID;
	}

	// search regions for right one
	Region *rspot = NULL;
	for (rspot = addrspace[as].regions; rspot != NULL; rspot = rspot->next)
	{
		if (rspot->ms == data->rec_num)
		{
			rspot->rights = rights;
			break;
		}
	}

	if (rspot == NULL)
		return BI_NAME_INVALID;
	else
		return 0;
}

/*
 * Only need new_cap, new_pool, and new_pd in order to
 * increase the current_ms.
 */
bi_name_t
bootinfo_new_cap(bi_name_t obj, bi_cap_rights_t rights,
		const bi_user_data_t * data) {
	current_ms++;
	return 0; // No idea what it's actually meant to return.
}

bi_name_t
bootinfo_new_pool(int is_virtual, const bi_user_data_t * data) {
	current_ms++;
	return 0; // No idea what it's actually meant to return.
}

bi_name_t
bootinfo_new_pd(bi_name_t owner, const bi_user_data_t * data) {
	current_ms++;
	return owner;
}

/* we abuse the bootinfo interface a bit by creating a new task on each
 * new thread operation
 */
static bi_name_t
bootinfo_new_thread(bi_name_t bi_owner, uintptr_t ip,
                    uintptr_t user_main, int priority,
                    char* name, size_t name_len,
                    const bi_user_data_t* data)
{
	// need to setup address space here, create a new address space with L4
	// then setup any regions we need for heap and stack. PageTable isn't touched
	// yet as we are using lazy allocation so we just want all the regions setup
	// correctly so that it will page fault and we can then map it in.
    void *sp = data->user_data;
    dprintf(0, "bootinfo_new_thread starting thread: %s, %d\n", name, bi_owner);

    // Start a new task with this program
    // (using the kernel thread id allocator for task ids too)
    L4_ThreadId_t newtid = sos_task_new(L4_ThreadNo(sos_get_new_tid()), L4_Pager(),
		    (void *) ip, sp);

	 // make sure newtid is within addressspace range should be ok though since L4 makes sure their unique
    if (newtid.raw != -1UL && newtid.raw != -2UL && newtid.raw != -3UL && sos_tid2task(newtid) < MAX_ADDRSPACES) {
        dprintf(0, "Created task: %lx\n", sos_tid2task(newtid));
    } else {
        dprintf(0, "sos_task_new failed: %d\n", newtid.raw);
        return BI_NAME_INVALID;
    }

	 current_ms++;
	 addrspace[sos_tid2task(newtid)].pd = bi_owner;
    return newtid.raw;
}

void
sos_start_binfo_executables(void *userstack)
{
    int result;
    bi_callbacks_t bi_callbacks = OKL4_BOOTINFO_CALLBACK_INIT;

    bi_callbacks.new_thread = bootinfo_new_thread;
    bi_callbacks.new_ms = bootinfo_new_ms;
    bi_callbacks.attach = bootinfo_attach;
	 bi_callbacks.new_cap = bootinfo_new_cap;
	 bi_callbacks.new_pool = bootinfo_new_pool;
	 bi_callbacks.new_pd = bootinfo_new_pd;
    
    result = bootinfo_parse(__okl4_bootinfo, &bi_callbacks, userstack);
    if (result)
        dprintf(0, "bootinfo_parse failed: %d\n", result);
}

// Memory for the ixp400 networking layers
extern void *sos_malloc(uint32_t size);
void *sos_malloc(uint32_t size)
{
    if (sSosMemoryBot + size < sSosMemoryTop) {
        L4_Word_t bot = sSosMemoryBot;
        sSosMemoryBot += size;
        return (void *) bot;
    } else {
        dprintf(0, "sos_malloc failed\n");
        return (void *) 0;
	}
}

void sos_usleep(uint32_t microseconds)
{
    utimer_sleep(microseconds);	// M4 must change to your timer
}
