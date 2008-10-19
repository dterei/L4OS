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

#include <nfs/nfs.h>
#include <serial/serial.h>
#include <sos/sos.h>

#include "constants.h"
#include "l4.h"
#include "pager.h"
#include "process.h"
#include "region.h"
#include "timer.h"
#include "vfs.h"

#include "libsos.h"

#define VIRTPOOL_MAP_DIRECTLY 0x3
#define CLIST_LOCAL_ID 14
#define CLIST_USER_ID 15

#define verbose 1

extern void _start(void);

static L4_Word_t sSosMemoryTop, sSosMemoryBot;

static L4_Fpage_t utcb_fpage_s;
static L4_Word_t utcb_base_s;
static L4_Word_t last_thread_s;
static L4_Word_t last_thread_disabled_s;

// Address of the bootinfo buffer
extern void *__okl4_bootinfo;

// That odd (Iguana/bootinfo) way of identifying memory sections.
static bi_name_t bootinfo_id = 1;

typedef struct BootinfoRegion_t {
	Region *region;
	bi_name_t id;
	struct BootinfoRegion_t *next;
} BootinfoRegion;

typedef struct BootinfoProcess_t {
	Process *process;
	BootinfoRegion *regions;
	bi_name_t tid; // needed to run the thread on callback
	bi_name_t sid; // needed to identify the address space
	struct BootinfoProcess_t *next;
} BootinfoProcess;

// List of processes from bootinfo
static BootinfoProcess *bips = NULL;

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
        dprintf(1, "UTCB at %lx l2sz %lx\n", utcb_base_s, utcb_fpage_s.raw);
    }
    else {  // No userland utcb map
        utcb_fpage_s = L4_Nilpage;
        utcb_base_s      = -1UL;
        dprintf(1, "UTCB under kernel control\n");
    }

    dprintf(1, "BootInfo at %p\n", __okl4_bootinfo);
    
    /* allow ourself to control each of the interrupts */
    for (int i = 0 ; i < 32 ; i++) {
        L4_LoadMR(0, i);
        L4_Word_t success = L4_AllowInterruptControl(L4_rootspace);
        assert(success);
    }

    //utimer_init();

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
    result = bootinfo_parse(__okl4_bootinfo, &bi_callbacks);
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

void
sos_print_l4memory(void *addr, L4_Word_t len)
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

void
sos_logf(const char *msg, ...)
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
sos_get_new_tid(void) {
	if (!last_thread_disabled_s) {
		dprintf(3, "allocating new tid %lu\n", last_thread_s+1);
		return L4_GlobalId(++last_thread_s, 1);
	} else {
		dprintf(0, "sos_get_new_tid has been disabled!\n");
		return L4_nilthread;
	}
}

void
sos_get_new_tid_disable(void) {
	last_thread_disabled_s = 1;
}

// get the next thread id that will be issued
L4_ThreadId_t
sos_peek_new_tid(void) {
	return L4_GlobalId(last_thread_s+1, 1);
}

// Create a new thread
static inline L4_ThreadId_t
create_thread(L4_ThreadId_t tid, L4_ThreadId_t scheduler) {
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
sos_thread_new_priority(L4_ThreadId_t tid, L4_Word_t prio,
		void *entry, void *stack) {
	// SOS: we now assign thread id externally, same as task
	if (L4_IsThreadEqual(tid, L4_nilthread)) {
		tid = sos_get_new_tid();
	}

	L4_ThreadId_t sched = prio ? L4_rootserver : L4_anythread;

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
sos_thread_new(L4_ThreadId_t tid, void *entrypoint, void *stack) {
	return sos_thread_new_priority(tid, 0, entrypoint, stack);
}

static L4_ClistId_t localClist(void) {
	static int haveAllocated = 0;
	static L4_ClistId_t clist;

	assert(pager_is_active());

	if (!haveAllocated) {
		clist = L4_ClistId(CLIST_LOCAL_ID);
		please(L4_CreateClist(clist, 32));
		please(L4_CreateIpcCap(L4_rootserver, L4_rootclist, L4_rootserver, clist));
	}

	return clist;
}

static L4_ClistId_t userClist(void) {
	static int haveAllocated = 0;
	static L4_ClistId_t clist;

	assert(pager_is_active());

	if (!haveAllocated) {
		clist = L4_ClistId(CLIST_USER_ID);
		please(L4_CreateClist(clist, 32));
		please(L4_CreateIpcCap(L4_rootserver, L4_rootclist, L4_rootserver, clist));
		please(L4_CreateIpcCap(pager_get_tid(), L4_rootclist, pager_get_tid(), clist));
		haveAllocated = 1;
	}

	return clist;
}

// Create and start a new task
L4_ThreadId_t
sos_task_new(L4_Word_t task, L4_ThreadId_t pager, 
		void *entrypoint, void *stack) {
	// HACK: Workaround for compiler bug, volatile qualifier stops the internal
	// compiler error.
	L4_SpaceId_t spaceId = L4_SpaceId(task);
	L4_ClistId_t clistId;

	if (pager_is_active() && L4_IsThreadEqual(pager, pager_get_tid())) {
		clistId = userClist();
	} else {
		clistId = localClist();
	}

	please(L4_SpaceControl(spaceId, L4_SpaceCtrl_new, clistId, utcb_fpage_s, 0, NULL));

	L4_ThreadId_t tid = L4_GlobalId(task, 1);
	please(L4_ThreadControl(tid, spaceId, L4_rootserver, pager, pager, 0, (void*) utcb_base_s));

	L4_Start_SpIp(tid, (L4_Word_t) stack, (L4_Word_t) entrypoint);

	return tid;
}

/******************
 * BOOTINFO STUFF *
 ******************/

static void
addRegion(BootinfoProcess *bip, Region *r, bi_name_t id) {
	BootinfoRegion *new = (BootinfoRegion*) malloc(sizeof(BootinfoRegion));
	new->region = r;
	new->id = id;
	new->next = bip->regions;
	bip->regions = new;
}

static bi_name_t
bootinfo_new_ms(bi_name_t owner, uintptr_t base, uintptr_t size,
		uintptr_t flags, uintptr_t attr, bi_name_t physpool,
		bi_name_t virtpool, bi_name_t zone, const bi_user_data_t * data) {
	dprintf(2, "*** bootinfo_new_ms: (owner %d) = %d\n", owner, bootinfo_id);

	if (owner == 0) {
		dprintf(2, "*** bootinfo_new_ms: ignoring owner of 0\n");
		return ++bootinfo_id;
	}

	// Look for which address space it wants.
	BootinfoProcess *bip;

	for (bip = bips; bip != NULL; bip = bip->next) {
		if (bip->sid == owner) break;
	}

	if (bip == NULL) {
		dprintf(0, "!!! bootinfo_new_ms: didn't find relevant process!\n");
		return ++bootinfo_id;
	} else {
		dprintf(2, "*** bootinfo_new_ms: found process at %p\n", bip);
	}

	// Create new region.
	int dirmap = (virtpool == VIRTPOOL_MAP_DIRECTLY);
	Region *new = region_alloc(REGION_OTHER, base, size, 0, dirmap);
	addRegion(bip, new, bootinfo_id);

	return ++bootinfo_id;
}

static int
bootinfo_attach(bi_name_t pd, bi_name_t ms, int rights,
		const bi_user_data_t *data) {
	dprintf(2, "*** bootinfo_attach: (pd %d, ms %d) = %d\n", pd, ms, bootinfo_id);

	if (pd == 0) {
		dprintf(2, "*** bootinfo_attach: ignoring pd of 0\n");
		return 0;
	}

	// Look for which thread (address space) it wants.
	BootinfoProcess *bip;

	for (bip = bips; bip != NULL; bip = bip->next) {
		if (bip->sid == pd) break;
	}

	if (bip == NULL) {
		dprintf(0, "!!! bootinfo_attach: didn't find relevant process!\n");
		return BI_NAME_INVALID;
	} else {
		dprintf(2, "*** bootinfo_attach: found process at %p\n", bip);
	}

	// Look for the region.
	BootinfoRegion *region;

	for (region = bip->regions; region != NULL; region = region->next) {
		if (region->id == ms) break;
	}

	if (region == NULL) {
		dprintf(0, "!!! bootinfo_attach: didn't find relevant region!\n");
		return BI_NAME_INVALID;
	} else {
		dprintf(2, "*** bootinfo_attach: found relevant region at %p\n", region);
	}

	// Make necessary changes to the region.
	region_set_rights(region->region, rights);

	return 0;
}

static bi_name_t
bootinfo_new_cap(bi_name_t obj, bi_cap_rights_t rights,
		const bi_user_data_t *data) {
	return ++bootinfo_id;
}

static bi_name_t
bootinfo_new_pool(int is_virtual, const bi_user_data_t * data) {
	return ++bootinfo_id;
}

static bi_name_t
bootinfo_new_pd(bi_name_t owner, const bi_user_data_t * data) {
	dprintf(2, "*** bootinfo_new_pd: (owner %d) = %d\n", owner, bootinfo_id);

	BootinfoProcess *bip = (BootinfoProcess*) malloc(sizeof(BootinfoProcess));
	bip->process = process_init(0);
	bip->regions = NULL;
	//bip->tid = found later
	bip->sid = bootinfo_id;
	bip->next = bips;
	bips = bip;

	return ++bootinfo_id;
}

static bi_name_t
bootinfo_new_thread(bi_name_t bi_owner, uintptr_t ip,
		uintptr_t user_main, int priority, char* name,
		size_t name_len, const bi_user_data_t *data) {
	dprintf(2, "*** bootinfo_new_thread: (owner %d) = %d\n", bi_owner, bootinfo_id);

	if (bi_owner == 0) {
		dprintf(2, "*** bootinfo_new_thread: ignoring owner of 0\n");
		return 0;
	}

	// Find pd that owns this thread (and by doing so the thread info).
	BootinfoProcess *bip;
	for (bip = bips; bip != NULL; bip = bip->next) {
		if (bip->sid == bi_owner) break;
	}

	if (bip == NULL) {
		dprintf(0, "!!! bootinfo_new_thread: didn't find process!\n");
		return ++bootinfo_id;
	}

	// Fill in some more info.
	bip->tid = bootinfo_id;
	process_set_name(bip->process, name);
	process_set_ip(bip->process, (void*) ip);

	return ++bootinfo_id;
}

static int
bootinfo_run_thread(bi_name_t tid, const bi_user_data_t *data) {
	dprintf(2, "*** bootinfo_run_thread: (tid %d) = %d\n", tid, bootinfo_id);

	// Find the process to run.
	BootinfoProcess *bip;
	for (bip = bips; bip != NULL; bip = bip->next) {
		if (bip->tid == tid) break;
	}

	if (bip == NULL) {
		dprintf(0, "!!! bootinfo_run_bip: didn't find matching process!\n");
		return BI_NAME_INVALID;
	}

	// Add all the regions to that process
	BootinfoRegion *region;
	for (region = bip->regions; region != NULL; region = region->next) {
		process_add_region(bip->process, region->region);
	}

	// Prepare and run the process
	process_prepare(bip->process);
	L4_ThreadId_t newtid = process_run(bip->process, YES_TIMESTAMP);
	dprintf(2, "*** bootinfo_run_thread: process_run gave me %d\n", L4_ThreadNo(newtid));

	if (newtid.raw != -1UL && newtid.raw != -2UL && newtid.raw != -3UL) {
		dprintf(2, "Bootinfo created thread: %d\n", (int) L4_ThreadNo(newtid));
	} else {
		dprintf(2, "Bootinfo failed to create thread: %d\n", newtid.raw);
		return BI_NAME_INVALID;
	}

	return 0;
}

static int
bootinfo_cleanup(const bi_user_data_t *data) {
	dprintf(2, "*** bootinfo_cleanup\n");

	BootinfoProcess *freeMe;
	BootinfoRegion *freeMeToo;

	while (bips != NULL) {
		while (bips->regions != NULL) {
			freeMeToo = bips->regions->next;
			free(bips->regions);
			bips->regions = freeMeToo;
		}

		freeMe = bips->next;
		free(bips);
		bips = freeMe;
	}

	dprintf(2, "*** bootinfo_cleanup done\n");

	return 0;
}

void
sos_start_binfo_executables() {
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

	result = bootinfo_parse(__okl4_bootinfo, &bi_callbacks);
	if (result) dprintf(0, "bootinfo_parse failed: %d\n", result);
}

// Memory for the ixp400 networking layers
extern void *sos_malloc(uint32_t size);

void
*sos_malloc(uint32_t size)
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

void
sos_usleep(uint32_t microseconds)
{
	utimer_sleep(microseconds);	// M4 must change to your timer
}

L4_Word_t
getCurrentProcNum(void)
{
	L4_SpaceId_t sp = L4_SenderSpace();
	if (L4_IsNilSpace(sp)) {
		return (-1);
	}

	L4_Word_t as = L4_SpaceNo(sp);
	if (as < 0 || as >= MAX_ADDRSPACES) {
		dprintf(0, "!!! Invalid Address Space Number! Outside range! (%d)\n", as);
		return (-1);
	}

	return as;
}

void
msgClearWith(L4_Word_t x)
{
	L4_Msg_t clear;
	L4_MsgClear(&clear);
	L4_MsgAppendWord(&clear, x);
	L4_MsgLoad(&clear);
}

