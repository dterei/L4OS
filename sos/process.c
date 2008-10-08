#include <clock/clock.h>
#include <sos/sos.h>
#include <string.h>

#include "constants.h"
#include "frames.h"
#include "l4.h"
#include "libsos.h"
#include "process.h"
#include "syscall.h"

#define STDOUT_FN "console"

#define WAIT_ANYBODY (-1)
#define WAIT_NOBODY (-2)

#define verbose 1

// The equivalent of a PCB
struct Process_t {
	process_t     info;
	Pagetable    *pagetable;
	Region       *regions;
	void         *sp;
	void         *ip;
	timestamp_t   startedAt;
	VFile         files[PROCESS_MAX_FILES];
	pid_t         waitingOn;
	Process      *threadSpace;
};

static struct Process_t EMPTY_PCB;

// Array of all PCBs
static Process *pcbs[MAX_ADDRSPACES];

// The next pid to allocate
static L4_Word_t nextTno = PROCESS_BASE_PID;

static L4_Word_t getNextTno(void) {
	L4_Word_t oldPid = nextTno;
	int firstIteration = 1;

	while (pcbs[nextTno] != NULL) {
		// Detect loop, no more pids!
		if (!firstIteration && oldPid == nextTno) {
			dprintf(0, "!!! getNextTno: none left\n");
			break;
		}

		nextTno++;

		// Gone past the end, loop around
		if (nextTno > MAX_THREADS) {
			nextTno = PROCESS_BASE_PID;
		}

		firstIteration = 0;
	}

	// Should probably throw error here instead, but
	// it's too much effort
	assert(pcbs[nextTno] == NULL);

	return nextTno;
}

static int isThread(Process *p) {
	return p->threadSpace != NULL;
}

Process *process_lookup(L4_Word_t key) {
	Process *p = pcbs[key];

	printf("process_lookup found %p\n", p);

	if (p == NULL) {
		return NULL;
	} else if (isThread(p)) {
		return p->threadSpace;
	} else {
		return p;
	}
}

static Process *processAlloc(void) {
	Process *p = (Process*) malloc(sizeof(Process));

	*p = EMPTY_PCB;
	p->pagetable = pagetable_init();
	vfiles_init(p->files);
	p->waitingOn = WAIT_NOBODY;

	return p;
}

Process *process_init(void) {
	return processAlloc();
}

Process *thread_init(Process *p) {
	Process *thread = (Process*) malloc(sizeof(struct Process_t));
	*thread = EMPTY_PCB;

	assert(p != NULL);
	thread->threadSpace = p;
	assert(thread->threadSpace != NULL);

	thread->info.pid = getNextTno();
	pcbs[thread->info.pid] = thread;

	return thread;
}


void process_add_region(Process *p, Region *r) {
	assert(!isThread(p));
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
	assert(!isThread(p));
	strncpy(p->info.command, name, N_NAME);
}

static void addRegion(Process *p, region_type type,
		uintptr_t base, uintptr_t size, int rights, int dirmap) {
	assert(!isThread(p));
	Region *new = region_init(type, base, size, rights, dirmap);
	region_append(new, p->regions);
	p->regions = new;
}

static void addBuiltinRegions(Process *p) {
	assert(!isThread(p));
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

void process_dump(Process *p) {
	printf("*** %s on %d at %p\n", __FUNCTION__, p->info.pid, p);
	printf("*** %s: pagetable: %p\n", __FUNCTION__, p->pagetable);

	for (Region *r = p->regions; r != NULL; r = region_next(r)) {
		printf("*** %s: region %p -> %p (%p)\n", __FUNCTION__,
				(void*) region_base(r), (void*) (region_base(r) + region_size(r)),
				(void*) r);
	}

	printf("*** %s: sp: %p\n", __FUNCTION__, p->sp);
	printf("*** %s: ip: %p\n", __FUNCTION__, p->ip);
}

void process_prepare(Process *p) {
	assert(!isThread(p));

	// Add the builtin regions (stack, heap)
	// This will set the stack pointer too
	addBuiltinRegions(p);

	// Register with the collection of PCBs
	p->info.pid = getNextTno();
	pcbs[p->info.pid] = p;

	// Open stdout
	vfs_open(process_get_tid(p), STDOUT_FN, FM_WRITE);
}

L4_ThreadId_t process_run(Process *p) {
	L4_ThreadId_t tid;
	if (verbose > 1) process_dump(p);

	p->startedAt = time_stamp();

	if (isThread(p)) {
		tid = sos_thread_new(process_get_tid(p), p->ip, p->sp);
	} else if (L4_IsThreadEqual(virtual_pager, L4_nilthread)) {
		tid = sos_task_new(p->info.pid, L4_Pager(), p->ip, p->sp);
	} else {
		tid = sos_task_new(p->info.pid, virtual_pager, p->ip, p->sp);
	}

	dprintf(0, "*** %s: running process %ld\n", __FUNCTION__, L4_ThreadNo(tid));
	assert(L4_IsThreadEqual(tid, process_get_tid(p)));

	return tid;
}

pid_t process_get_pid(Process *p) {
	if (p == NULL) {
		return NIL_PID;
	} else if (isThread(p)) {
		return process_get_pid(p->threadSpace);
	} else {
		return p->info.pid;
	}
}

L4_ThreadId_t process_get_tid(Process *p) {
	if (p == NULL) {
		return L4_nilthread;
	} else {
		return L4_GlobalId(p->info.pid, 1);
	}
}

L4_SpaceId_t process_get_sid(Process *p) {
	if (p == NULL) {
		return L4_nilspace;
	} else {
		return L4_SpaceId(process_get_pid(p));
	}
}

Pagetable *process_get_pagetable(Process *p) {
	if (p == NULL) {
		return NULL;
	} else if (isThread(p)) {
		return process_get_pagetable(p->threadSpace);
	} else {
		return p->pagetable;
	}
}

Region *process_get_regions(Process *p) {
	if (p == NULL) {
		return NULL;
	} else if (isThread(p)) {
		return process_get_regions(p->threadSpace);
	} else {
		return p->regions;
	}
}

static void wakeAll(pid_t wakeFor, pid_t wakeFrom) {
	for (int i = PROCESS_BASE_PID; i < MAX_ADDRSPACES; i++) {
		if ((pcbs[i] != NULL) && (pcbs[i]->waitingOn == wakeFor)) {
			pcbs[i]->waitingOn = WAIT_NOBODY;
			dprintf(0, "*** wakeAll: waking %d\n", process_get_pid(pcbs[i]));
			syscall_reply(process_get_tid(pcbs[i]), wakeFrom);
		}
	}
}

int process_kill(Process *p) {
	printf("process_kill on %p\n", p);

	if (p == NULL) {
		return (-1);
	} else if (isThread(p)) {
		process_kill(p->threadSpace);
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
	 * 	! kill threads from that process
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
	VFile *pfiles = process_get_files(p);
	if (pfiles == NULL) {
		dprintf(0, "!!! Process we are trying to kill has NULL open file table (%d)\n",
				process_get_pid(p));
	} else {
		for (int fd = 0; fd < PROCESS_MAX_FILES; fd++) {
			if (pfiles[fd].vnode != NULL) {
				vfs_close(process_get_tid(p), fd);
			}
		}
				
	}

	// Free the used frames, page table, and regions
	//frames_free(p->info.pid);
	pagetable_free(p->pagetable);
	region_free_all(p->regions);

	// Freeing the PCB and setting to NULL is enough to
	// free the pid as well
	pid_t ghostPid = p->info.pid;
	free(p);
	pcbs[ghostPid] = NULL;

	// Wake all process waiting on this process (or any)
	wakeAll(ghostPid, ghostPid);
	wakeAll(WAIT_ANYBODY, ghostPid);

	return 0;
}

int process_write_status(process_t *dest, int n) {
	int count = 0;

	// Firstly, update the size of sos
	assert(pcbs[L4_rootserverno] != NULL);
	pcbs[L4_rootserverno]->info.size = frames_allocated() - sos_memuse();

	// Everything else has size dynamically updated, so just
	// write them all out
	for (int i = 0; i < MAX_ADDRSPACES && count < n; i++) {
		if (pcbs[i] != NULL && !isThread(pcbs[i])) {
			pcbs[i]->info.stime = (time_stamp() - pcbs[i]->startedAt);
			*dest = pcbs[i]->info;
			dest++;
			count++;
		}
	}

	return count;
}

process_t *process_get_info(Process *p) {
	// If necessary, uptime stime here too
	if (p == NULL) {
		return NULL;
	} else if (isThread(p)) {
		return process_get_info(p->threadSpace);
	} else {
		return &p->info;
	}
}

VFile *process_get_files(Process *p) {
	if (p == NULL) {
		return NULL;
	} else if (isThread(p)) {
		return process_get_files(p->threadSpace);
	} else {
		return p->files;
	}
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
	pcbs[L4_rootserverno] = rootserver;

	if (verbose > 1) process_dump(rootserver);
}

