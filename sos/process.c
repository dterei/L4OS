#include <string.h>

#include "l4.h"
#include "libsos.h"
#include "pager.h"
#include "process.h"
#include "vfs.h"

#define STDOUT_FN "console"

#define verbose 1

struct Process_t {
	process_t info;
	PageTable *pagetable;
	Region *regions;
	PagerRequest *prequest;
	void *sp;
	void *ip;
};

static Process *sos_procs[MAX_ADDRSPACES];

Process *process_lookup(L4_Word_t key) {
	return sos_procs[key];
}

Process *process_init(void) {
	Process *p = (Process*) malloc(sizeof(Process));

	p->info.pid = 0; // decide later
	p->info.size = 0;
	p->info.stime = 0; // decide later
	p->info.ctime = 0;
	p->info.command[0] = '\0'; // decide later

	p->pagetable = pagetable_init();
	p->regions = NULL;
	p->prequest = NULL;
	p->sp = NULL;
	p->ip = NULL;

	return p;
}

void process_add_region(Process *p, Region *r) {
	region_append(r, p->regions);
	p->regions = r;
}

void process_set_ip(Process *p, void *ip) {
	p->ip = ip;
}

void process_set_sp(Process *p, void *sp) {
	// Set the stack pointer to something else -
	// Only possible AFTER process_prepare and
	// BEFORE process_run.  Moving the sp is
	// necessary if a process is to run without
	// the use of virtual memory, although it
	// will then need to manage its memory
	// manually.  
	p->sp = sp;
}

void process_set_name(Process *p, char *name) {
	strncpy(p->info.command, name, N_NAME);
}

static void addRegion(Process *p, region_type type,
		uintptr_t base, uintptr_t size, int rights, int dirmap) {
	Region *new = region_init(type, base, size, rights, dirmap);
	region_append(new, p->regions);
	p->regions = new;
}

static void addBuiltinRegions(Process *p) {
	uintptr_t base = 0;

	// Find the highest address and put the heap above it
	for (Region *r = p->regions; r != NULL; r = region_next(r)) {
		if ((region_base(r) + region_size(r)) > base) {
			base = region_base(r) + region_size(r);
		}
	}

	if (base == 0) {
		// No regions yet!  This must be some kind of special process
		// creation (i.e. the pager) that needs to be in physical memory,
		// so just let it do its stuff and set the sp manually, etc.
		return;
	} else {
		dprintf(1, "Base is at %p\n", (void*) base);
	}

	base = ((base - 1) & PAGEALIGN) + PAGESIZE; // Page align up

	// Size of the region is zero for now, expand as we get syscalls
	addRegion(p, REGION_HEAP, base, 0, REGION_READ | REGION_WRITE, 0);

	// Put the stack half way up the address space - at the top
	// causes pagefaults, so halfway up seems like a nice compromise
	base = (((unsigned int) -1) >> 1) & PAGEALIGN;
	addRegion(p, REGION_STACK, base - ONE_MEG, ONE_MEG, REGION_READ | REGION_WRITE, 0);

	// Some times, 3 words are popped unvoluntarily so may as well just
	// always allow for this
	p->sp = (void*) (base - (3 * sizeof(L4_Word_t)));
}

static void process_dump(Process *p) {
	(void) process_dump;

	dprintf(1, "*** %s on %ld\n", __FUNCTION__, p->info.pid);
	dprintf(1, "*** %s: pagetable: %p\n", __FUNCTION__, p->pagetable);
	dprintf(1, "*** %s: regions: %p\n", __FUNCTION__, p->regions);

	for (Region *r = p->regions; r != NULL; r = region_next(r)) {
		dprintf(1, "*** %s: region %p -> %p (%p)\n", __FUNCTION__,
				region_base(r), region_base(r) + region_size(r), r);
	}

	dprintf(1, "*** %s: sp: %p\n", __FUNCTION__, p->sp);
	dprintf(1, "*** %s: ip: %p\n", __FUNCTION__, p->ip);
}

void process_prepare(Process *p) {
	// Add the builtin regions (stack, heap)
	// This will set the stack pointer too
	addBuiltinRegions(p);

	// Register with the collection of PCBs
	p->info.pid = L4_ThreadNo(sos_get_new_tid());
	sos_procs[p->info.pid] = p;

	// Open stdout
	int dummy;
	vfs_open(process_get_tid(p), STDOUT_FN, FM_WRITE, &dummy);
}

L4_ThreadId_t process_run(Process *p, int asThread) {
	L4_ThreadId_t tid;
	process_dump(p);

	if (asThread == RUN_AS_THREAD) {
		tid = sos_thread_new(process_get_tid(p), p->ip, p->sp);
	} else if (L4_IsThreadEqual(virtual_pager, L4_nilthread)) {
		tid = sos_task_new(p->info.pid, L4_Pager(), p->ip, p->sp);
	} else {
		tid = sos_task_new(p->info.pid, virtual_pager, p->ip, p->sp);
	}

	dprintf(1, "*** %s: running process %ld\n", __FUNCTION__, L4_ThreadNo(tid));
	assert(L4_ThreadNo(tid) == p->info.pid);

	return tid;
}

pid_t process_get_pid(Process *p) {
	return p->info.pid;
}

L4_ThreadId_t process_get_tid(Process *p) {
	return L4_GlobalId(p->info.pid, 1);
}

PageTable *process_get_pagetable(Process *p) {
	return p->pagetable;
}

Region *process_get_regions(Process *p) {
	return p->regions;
}

void process_set_prequest(Process *p, PagerRequest *pr) {
	p->prequest = pr;
}

PagerRequest *process_get_prequest(Process *p) {
	return p->prequest;
}

void process_kill(pid_t pid) {
	printf("process_kill(%u) not implemented\n", pid);

	// Free the process struct,
	// Close all the files (easier if in the struct),
	// Free up the pid
}

int process_write_status(process_t *dest, int n) {
	int count = 0;

	for (int i = 0; i < MAX_ADDRSPACES && count < n; i++) {
		if (sos_procs[i] != NULL) {
			*dest = sos_procs[i]->info;
			dest++;
			count++;
		}
	}

	return count;
}

