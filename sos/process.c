#include <clock/clock.h>
#include <sos/sos.h>
#include <string.h>

#include "constants.h"
#include "frames.h"
#include "l4.h"
#include "libsos.h"
#include "list.h"
#include "process.h"
#include "region.h"
#include "syscall.h"

#define STDOUT_FN "console"

#define WAIT_ANYBODY (-1)
#define WAIT_NOBODY (-2)

#define verbose 1

// The equivalent of a PCB
struct Process_t {
	process_t     info;
	Pagetable    *pagetable;
	List         *regions;
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
	while (sosProcs[key] == NULL) key--;
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
	p->regions = list_empty();
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
	list_push(p->regions, r);
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
	strncpy(p->info.command, name, MAX_FILE_NAME);
}

static void *regionFindHighest(void *contents, void *data) {
	Region *r = (Region*) contents;
	int top = region_get_base(r) + region_get_size(r);

	if (top > (int) data) {
		return (void*) top;
	} else {
		return data;
	}
}

static void addBuiltinRegions(Process *p) {
	uintptr_t base = (uintptr_t) list_reduce(p->regions, regionFindHighest, 0);

	if (base == 0) {
		// No regions yet!  This must be some kind of special process
		// creation (i.e. the pager) that needs to be in physical memory,
		// so just let it do its stuff and set the sp manually, etc.
		return;
	} else {
		dprintf(2, "Base is at %p\n", (void*) base);
	}

	base = ((base - 1) & PAGEALIGN) + PAGESIZE; // Page align up

	// Size of the region is zero for now, expand as we get syscalls
	list_push(p->regions, region_alloc(REGION_HEAP,
				base, 0, REGION_READ | REGION_WRITE, 0));

	// Put the stack half way up the address space - at the top
	// causes pagefaults, so halfway up seems like a nice compromise
	base = (((unsigned int) -1) >> 1) & PAGEALIGN;

	list_push(p->regions, region_alloc(REGION_STACK,
				base - ONE_MEG, ONE_MEG, REGION_READ | REGION_WRITE, 0));

	// Some times, 3 words are popped unvoluntarily so may as well just
	// always allow for this
	p->sp = (void*) (base - (3 * sizeof(L4_Word_t)));
}

static void regionsDump(void *contents, void *data) {
	Region *r = (Region*) contents;

	printf("*** region %p -> %p (%p)\n",
			(void*) region_get_base(r),
			(void*) (region_get_base(r) + region_get_size(r)),
			(void*) r);
}

void process_dump(Process *p) {
	printf("*** %s on %d at %p\n", __FUNCTION__, p->info.pid, p);
	printf("*** pagetable: %p\n", p->pagetable);
	list_iterate(p->regions, regionsDump, NULL);
	printf("*** sp: %p\n", p->sp);
	printf("*** ip: %p\n", p->ip);
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

void process_prepare(Process *p, int asThread) {
	// Register with the collection of PCBs
	p->info.pid = getNextPid();
	sosProcs[p->info.pid] = p;

	if (asThread != RUN_AS_THREAD) {
		addBuiltinRegions(p);
		vfs_open(process_get_tid(p), STDOUT_FN, FM_WRITE);
	}
}

L4_ThreadId_t process_run(Process *p, int asThread) {
	L4_ThreadId_t tid;
	if (verbose > 2) process_dump(p);

	p->startedAt = time_stamp();

	if (asThread == RUN_AS_THREAD) {
		tid = sos_thread_new(process_get_tid(p), p->ip, p->sp);
	} else if (pager_is_active()) {
		tid = sos_task_new(p->info.pid, pager_get_tid(), p->ip, p->sp);
	} else {
		tid = sos_task_new(p->info.pid, L4_Pager(), p->ip, p->sp);
	}

	L4_KDB_SetThreadName(tid, p->info.command);

	dprintf(1, "*** %s: running process %ld\n", __FUNCTION__, L4_ThreadNo(tid));
	assert(L4_ThreadNo(tid) == p->info.pid);

	return tid;
}

pid_t process_get_pid(Process *p) {
	if (p == NULL) return NIL_PID;
	return p->info.pid;
}

L4_ThreadId_t process_get_tid(Process *p) {
	if (p == NULL) return L4_nilthread;
	return L4_GlobalId(p->info.pid, 1);
}

L4_SpaceId_t process_get_sid(Process *p) {
	if (p == NULL) return L4_nilspace;
	return L4_SpaceId(process_get_pid(p));
}

Pagetable *process_get_pagetable(Process *p) {
	if (p == NULL) return NULL;
	return p->pagetable;
}

List *process_get_regions(Process *p) {
	if (p == NULL) return NULL;
	return p->regions;
}

static void wakeAll(pid_t wakeFor, pid_t wakeFrom) {
	for (int i = tidOffset; i < MAX_ADDRSPACES; i++) {
		if ((sosProcs[i] != NULL) && (sosProcs[i]->waitingOn == wakeFor)) {
			sosProcs[i]->waitingOn = WAIT_NOBODY;
			dprintf(0, "*** wakeAll: waking %d\n", process_get_pid(sosProcs[i]));
			syscall_reply(process_get_tid(sosProcs[i]), wakeFrom);
		}
	}
}

void process_wake_all(pid_t pid) {
	wakeAll(pid, pid);
	wakeAll(WAIT_ANYBODY, pid);
}

void process_close_files(Process *p) {
	VFile *pfiles = process_get_files(p);

	if (pfiles == NULL) {
		dprintf(0, "!!! process_close_files: %p has NULL open file table\n",
				process_get_pid(p));
	} else {
		for (int fd = 0; fd < PROCESS_MAX_FILES; fd++) {
			if (pfiles[fd].vnode != NULL) {
				vfs_close(process_get_tid(p), fd);
			}
		}
	}
}

int process_kill(Process *p) {
	assert(p != NULL);

	if (process_get_pid(p) > tidOffset) {
		// Isn't a kernel-allocated process, and isn't the pager
		please(L4_ThreadControl(process_get_tid(p), L4_nilspace, L4_nilthread,
					L4_nilthread, L4_nilthread, 0, NULL));
		sosProcs[process_get_pid(p)] = NULL;
		return 0;
	} else {
		// Invalid process
		return (-1);
	}
}

int process_write_status(process_t *dest, int n) {
	int count = 0;

	// Everything else has size dynamically updated, so just
	// write them all out
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
	if (p == NULL) return NULL;
	return &p->info;
}

VFile *process_get_files(Process *p) {
	if (p == NULL) return NULL;
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

	if (verbose > 2) process_dump(rootserver);
}

