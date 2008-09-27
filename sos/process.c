#include <clock/clock.h>
#include <sos/sos.h>
#include <string.h>

#include "l4.h"
#include "libsos.h"
#include "pager.h"
#include "process.h"
#include "syscall.h"
#include "vfs.h"

#define STDOUT_FN "console"

#define WAIT_ANYBODY (-1)
#define WAIT_NOBODY (-2)

#define verbose 1

// The equivalent of a PCB
struct Process_t {
	process_t     info;
	PageTable    *pagetable;
	Region       *regions;
	void         *sp;
	void         *ip;
	timestamp_t   startedAt;
	VFile         files[PROCESS_MAX_FILES];
	pid_t         waitingOn;
};

// Array of all PCBs
static Process *sosProcs[MAX_ADDRSPACES];

// The next pid to allocate
static L4_Word_t nextPid;

// All pids below this value have already been allocated by libsos
// as threadids, and all over SOS it has been assumed that there is
// a 1:1 mapping between pid and tid (and sid incidentally), so just
// never allocate a pid below this value.
static L4_Word_t tidOffset;

Process *process_lookup(L4_Word_t key) {
	return sosProcs[key];
}

static Process *processAlloc(void) {
	Process *p = (Process*) malloc(sizeof(Process));

	p->info.pid = 0;   // decide later
	p->info.size = 0;  // fill in as we go
	p->info.stime = 0; // decide later
	p->info.ctime = 0; // don't ever need
	p->info.command[0] = '\0';

	p->pagetable = pagetable_init();
	p->regions = NULL;
	p->sp = NULL;
	p->ip = NULL;
	vfiles_init(p->files);
	p->waitingOn = WAIT_NOBODY;

	return p;
}

Process *process_init(void) {
	// On the first process initialisation set the offset
	// to whatever it is, and disable libsos from allocating
	// any more threadids.  Admittedly, this is a bit of a hack.
	if (tidOffset == 0) {
		tidOffset = L4_ThreadNo(sos_peek_new_tid());
		sos_get_new_tid_disable();
		nextPid = tidOffset;
	}

	// Do the normal process initialisation
	return processAlloc();
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

	printf("*** %s on %d\n", __FUNCTION__, p->info.pid);
	printf("*** %s: pagetable: %p\n", __FUNCTION__, p->pagetable);
	printf("*** %s: regions: %p\n", __FUNCTION__, p->regions);

	for (Region *r = p->regions; r != NULL; r = region_next(r)) {
		printf("*** %s: region %p -> %p (%p)\n", __FUNCTION__,
				(void*) region_base(r), (void*) (region_base(r) + region_size(r)),
				(void*) r);
	}

	printf("*** %s: sp: %p\n", __FUNCTION__, p->sp);
	printf("*** %s: ip: %p\n", __FUNCTION__, p->ip);
}

static L4_Word_t getNextPid(void) {
	L4_Word_t oldPid = nextPid;
	int firstIteration = 1;

	while (sosProcs[nextPid] != NULL) {
		// Detect loop, no more pids!
		if (!firstIteration && oldPid == nextPid) {
			dprintf(0, "!!! getNextPid: none left\n");
			break;
		}

		nextPid++;

		// Gone past the end, loop around
		if (nextPid > MAX_THREADS) {
			nextPid = tidOffset;
		}

		firstIteration = 0;
	}

	// Should probably throw error here instead, but
	// it's too much effort
	assert(sosProcs[nextPid] == NULL);

	return nextPid;
}

void process_prepare(Process *p) {
	// Add the builtin regions (stack, heap)
	// This will set the stack pointer too
	addBuiltinRegions(p);

	// Register with the collection of PCBs
	p->info.pid = getNextPid();
	sosProcs[p->info.pid] = p;

	// Open stdout
	int dummy;
	vfs_open(process_get_tid(p), STDOUT_FN, FM_WRITE, &dummy);
}

L4_ThreadId_t process_run(Process *p, int asThread) {
	L4_ThreadId_t tid;
	if (verbose > 1) process_dump(p);

	p->startedAt = time_stamp();

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

static void wakeAll(pid_t wakeFor, pid_t wakeFrom) {
	for (int i = tidOffset; i < MAX_ADDRSPACES; i++) {
		if ((sosProcs[i] != NULL) && (sosProcs[i]->waitingOn == wakeFor)) {
			sosProcs[i]->waitingOn = WAIT_NOBODY;
			syscall_reply(process_get_tid(sosProcs[i]), wakeFrom);
		}
	}
}

int process_kill(Process *p) {
	printf("process_kill on %p\n", p);

	if (p == NULL) {
		return (-1);
	} else {
		printf("pid is %d\n", p->info.pid);
	}

	/*
	 * Will need to:
	 * 	- kill thread
	 * 	- close all files
	 * 	! free all frames allocted by the process
	 * 	! free all swapped frames
	 * 	- free page table
	 * 	- free regions
	 * 	- free up the pid for another process
	 * 	- free the PCB
	 * 	- wake up waiting processes
	 *
	 * Points marked with (!) are ones that haven't been
	 * done yet.
	 *
	 * Won't need to:
	 * 	- free the pager request (will happen by itself)
	 * 	- worry about stray messages (sycall_reply is ok)
	 */

	// Kill all threads associated with the process
	L4_ThreadControl(process_get_tid(p), L4_nilspace, L4_nilthread,
			L4_nilthread, L4_nilthread, 0, NULL);

	// Close all files opened by it
	int dummy;

	for (int fd = 0; fd < PROCESS_MAX_FILES; fd++) {
		vfs_close(process_get_tid(p), fd, &dummy);
	}

	// Free the used frames, page table, and regions
	//frames_free(p->info.pid);
	pagetable_free(p->pagetable);
	region_free_all(p->regions);

	// Freeing the PCB and setting to NULL is enough to
	// free the pid as well
	pid_t ghostPid = p->info.pid;
	free(p);
	sosProcs[ghostPid] = NULL;

	// Wake all process waiting on this process (or any)
	wakeAll(ghostPid, ghostPid);
	wakeAll(WAIT_ANYBODY, ghostPid);

	return 0;
}

int process_write_status(process_t *dest, int n) {
	int count = 0;

	for (int i = 0; i < MAX_ADDRSPACES && count < n; i++) {
		if (sosProcs[i] != NULL) {
			sosProcs[i]->info.stime = (time_stamp() - sosProcs[i]->startedAt);
			*dest = sosProcs[i]->info;
			dest++;
			count++;
		}
	}

	return count;
}

process_t *process_get_info(Process *p) {
	// If necessary, uptime stime here too
	return &p->info;
}

VFile *process_get_files(Process *p) {
	return p->files;
}

void process_wait_any(Process *waiter) {
	waiter->waitingOn = WAIT_ANYBODY;
}

void process_wait_for(Process *waitFor, Process *waiter) {
	waiter->waitingOn = process_get_pid(waitFor);
}

void process_add_rootserver(void) {
	Process *rootserver = processAlloc();

	rootserver->startedAt = time_stamp();
	process_set_name(rootserver, "sos");

	assert(rootserver->info.pid == L4_rootserverno);
	sosProcs[L4_rootserverno] = rootserver;

	if (verbose > 1) process_dump(rootserver);
}

