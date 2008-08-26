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

#define VIRTPOOL_MAP_DIRECTLY 0x3

// Hack externs from timer.c
extern void utimer_init(void);
extern void utimer_sleep(uint32_t microseconds);

#define verbose 1

extern void _start(void);

static L4_Word_t sSosMemoryTop, sSosMemoryBot;

static L4_Fpage_t utcb_fpage_s;
static L4_Word_t utcb_base_s;
static L4_Word_t last_thread_s;

// Address of the bootinfo buffer
extern void *__okl4_bootinfo;

// That odd (Iguana/bootinfo) way of identifying memory sections.
static bi_name_t bootinfo_id = 1;

// List of threads as identified by bootinfo.
typedef struct ThreadListT *ThreadList;
struct ThreadListT {
	bi_name_t tid;       // thread id as assigned by bootinfo
	bi_name_t pd;        // pd (i.e. as) as assigned by bootinfo
	L4_ThreadId_t sosid; // the id we (as sos) will give it
	uintptr_t ip;        // ip for when thread is started
	void *sp;            // stack for when thread is started
	ThreadList next;
};

static ThreadList threads = NULL;

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
	dprintf(2, "*** bootinfo_init_mem\n");
	sSosMemoryBot = phys_base;
	sSosMemoryTop  = phys_end;
	bootinfo_id += 3;
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
	L4_ThreadId_t tid = L4_GlobalId(task, 1);
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
	dprintf(1, "*** bootinfo_new_ms: (owner %d) = %d\n", owner, bootinfo_id);

	if (owner == 0) {
		dprintf(1, "*** bootinfo_new_ms: ignoring owner of 0\n");
		return ++bootinfo_id;
	}

	// Look for which thread (address space) it wants.
	ThreadList thread;

	for (thread = threads; thread != NULL; thread = thread->next) {
		if (thread->pd == owner) break;
	}

	if (thread == NULL) {
		dprintf(0, "!!! bootinfo_new_ms: didn't find relevant thread!\n");
		return ++bootinfo_id;
	} else {
		dprintf(1, "*** bootinfo_new_ms: found thread %d\n",
				L4_ThreadNo(thread->sosid));
	}

	// Create new region.
	Region *newreg = (Region *)malloc(sizeof(Region));
	newreg->base = base;
	newreg->size = size;
	newreg->mapDirectly = (virtpool == VIRTPOOL_MAP_DIRECTLY);
	newreg->rights = 0;
	newreg->id = bootinfo_id;

	dprintf(1, "*** bootinfo_new_ms: created new ms with id=%d\n", newreg->id);

	// Add region to that address space.
	AddrSpace *as = &addrspace[L4_ThreadNo(thread->sosid)];

	newreg->next = as->regions;
	as->regions = newreg;

	return ++bootinfo_id;
}

int
bootinfo_attach(bi_name_t pd, bi_name_t ms, int rights,
		const bi_user_data_t *data)
{
	dprintf(1, "*** bootinfo_attach: (pd %d, ms %d) = %d\n", pd, ms, bootinfo_id);

	if (pd == 0) {
		dprintf(1, "*** bootinfo_attach: ignoring pd of 0\n");
		return 0;
	}

	// Look for which thread (address space) it wants.
	ThreadList thread;

	for (thread = threads; thread != NULL; thread = thread->next) {
		if (thread->pd == pd) break;
	}

	if (thread == NULL) {
		dprintf(0, "!!! bootinfo_attach: didn't find relevant thread!\n");
		return BI_NAME_INVALID;
	} else {
		dprintf(1, "*** bootinfo_attach: found thread %d\n",
				L4_ThreadNo(thread->sosid));
	}

	// Look for the region.
	Region *region = addrspace[L4_ThreadNo(thread->sosid)].regions;

	for (; region != NULL; region = region->next) {
		if (region->id == ms) break;
	}

	if (region == NULL) {
		dprintf(0, "!!! bootinfo_attach: didn't find relevant ms!\n");
		return BI_NAME_INVALID;
	} else {
		dprintf(1, "*** bootinfo_attach: found relevant ms at %p\n", thread);
	}

	// Make necessary changes to the region.
	region->rights = rights;

	return 0;
}

bi_name_t
bootinfo_new_cap(bi_name_t obj, bi_cap_rights_t rights,
		const bi_user_data_t *data) {
	return ++bootinfo_id;
}

bi_name_t
bootinfo_new_pool(int is_virtual, const bi_user_data_t * data) {
	return ++bootinfo_id;
}

bi_name_t
bootinfo_new_pd(bi_name_t owner, const bi_user_data_t * data) {
	dprintf(1, "*** bootinfo_new_pd: (owner %d) = %d\n", owner, bootinfo_id);

	// Record the existence of the new pd via the creation of a
	// new thread info struct.  Will fill this in as we go through
	// the various callbacks.
	ThreadList newThread = (ThreadList) malloc(sizeof(struct ThreadListT));
	newThread->pd = bootinfo_id;
	newThread->next = threads;
	threads = newThread;

	return ++bootinfo_id;
}

static bi_name_t
bootinfo_new_thread(bi_name_t bi_owner, uintptr_t ip,
                    uintptr_t user_main, int priority,
                    char* name, size_t name_len,
                    const bi_user_data_t *data)
{
	dprintf(1, "*** bootinfo_new_thread: (owner %d) = %d\n", bi_owner, bootinfo_id);

	if (bi_owner == 0) {
		dprintf(1, "*** bootinfo_new_thread: ignoring owner of 0\n");
		return 0;
	}

	// Find pd that owns this thread (and by doing so the thread info).
	ThreadList thread;
	for (thread = threads; thread != NULL; thread = thread->next) {
		if (thread->pd == bi_owner) break;
	}

	if (thread == NULL) {
		dprintf(0, "!!! bootinfo_new_thread: didn't find pd owner!\n");
		return ++bootinfo_id;
	}

	// Fill in some more info.
	thread->tid = bootinfo_id;
	thread->ip = ip;
	thread->sp = data->user_data;
	thread->sosid = sos_get_new_tid();
	dprintf(1, "I was just allocated threadid %lx\n", L4_ThreadNo(thread->sosid));

	// Now would be a good time to initialise our address space properly.
	AddrSpace *as = &addrspace[L4_ThreadNo(thread->sosid)];
	as->pagetb = (PageTable1 *) malloc(sizeof(PageTable1));
	as->regions = NULL;

	for (int i = 0; i < PAGETABLE_SIZE1; i++)
	{
		as->pagetb->pages2[i] = NULL;
	}

	return ++bootinfo_id;
}

int
bootinfo_run_thread(bi_name_t tid, const bi_user_data_t *data) {
	dprintf(1, "*** bootinfo_run_thread: (tid %d) = %d\n", tid, bootinfo_id);

	// Find the thread to run.
	ThreadList thread;
	for (thread = threads; thread != NULL; thread = thread->next) {
		if (thread->tid == tid) break;
	}

	if (thread == NULL) {
		dprintf(0, "!!! bootinfo_run_thread: didn't find matching thread!\n");
		return BI_NAME_INVALID;
	}

	// Start a new task from the thread.
	dprintf(1, "*** bootinfo_run_thread: trying to start thread %d\n", L4_ThreadNo(thread->sosid));

	AddrSpace *as = &addrspace[L4_ThreadNo(thread->sosid)];
	uintptr_t sp = add_stackheap(as);

	if (sp == 0)
		return BI_NAME_INVALID;

	L4_ThreadId_t newtid = sos_task_new(L4_ThreadNo(thread->sosid), L4_Pager(),
			(void*) thread->ip, (void*) sp);

	dprintf(1, "*** bootinfo_run_thread: sos_task_new gave me %d\n", L4_ThreadNo(newtid));

	if (newtid.raw != -1UL && newtid.raw != -2UL && newtid.raw != -3UL) {
		dprintf(0, "Created task: %lx\n", sos_tid2task(newtid));
	} else {
		dprintf(0, "sos_task_new failed: %d\n", newtid.raw);
		return BI_NAME_INVALID;
	}

	return 0;
}

int
bootinfo_cleanup(const bi_user_data_t *data) {
	dprintf(1, "*** bootinfo_cleanup\n");

	ThreadList freeMe;
	while (threads != NULL) {
		freeMe = threads->next;
		free(threads);
		threads = freeMe;
	}

	return 0;
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
	bi_callbacks.run_thread = bootinfo_run_thread;
	bi_callbacks.cleanup = bootinfo_cleanup;

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
