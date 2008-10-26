#include <elf/elf.h>
#include <sos/sos.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "constants.h"
#include "frames.h"
#include "l4.h"
#include "libsos.h"
#include "list.h"
#include "pager.h"
#include "pair.h"
#include "process.h"
#include "region.h"
#include "swapfile.h"
#include "syscall.h"

#define verbose 3

// Masks for page table entries
#define SWAP_MASK (1 << 0)
#define REF_MASK  (1 << 1)
#define MMAP_MASK (1 << 2)
#define ELF_MASK  (1 << 3)
#define TEMP_MASK (1 << 4)
#define ADDRESS_MASK PAGEALIGN

// Limiting the number of user frames
#define FRAME_ALLOC_LIMIT 42 // XXX
static int allocLimit; // XXX

// The core of the Pager's asynchronous requests
typedef struct Request {
	// Stage in request continuations, passed to cont.
	int stage;

	// OO-style data associated with the request.
	void *data;

	// Called when this request is first run, returns the stage to start at.
	int (*init)(void *data);

	// Called each time a continuation arrives for this thread with data from
	// the Request and the return value from the IPC.
	// Returns the number of the next stage or FINISHED to finish.
	int (*cont)(void *data, int stage, int rval);

	// Called when request finishes.
	void (*finish)(void *data);
};

// Tracking allocated frames, including default swap file
static List *alloced; // [(pid, word)]
static List *swapped; // [(pid, word)]
static List *mmapped; // [MMap]

static Swapfile *defaultSwapfile;

// Tracking mmapped frames
typedef struct MMap_t {
	pid_t pid;
	L4_Word_t memAddr;
	L4_Word_t dskAddr;
	size_t size;
	int rights;
	char path[MAX_FILE_NAME];
} MMap;

static List *requests; // [(rtype_t, rdata)]

static void queueRequests(int n, ...);
static void queueRequest2(Request *req);
static void dequeueRequest2(void);
static void startRequest2(void);
static void startMMapRead(void);
static void startRead(void);
static void startWrite(void);

// For reading and writing
typedef struct ReadRequest_t {
	pid_t pid;
	L4_Word_t dst;
	L4_Word_t src;
	int size;
	int offset;
	char path[MAX_FILE_NAME];
	fildes_t fd;
	int alwaysOpen;
	int rights;
} ReadRequest;
typedef ReadRequest WriteRequest;

typedef enum {
	STAGE_OPEN,
	STAGE_STAT,
	STAGE_LSEEK,
	STAGE_READ,
	STAGE_CLOSE
} read_stage_t;
typedef read_stage_t write_stage_t;

#define STAGE_WRITE STAGE_READ
#define allocWriteRequest allocReadRequest

typedef struct Pagefault_t Pagefault;
struct Pagefault_t {
	pid_t pid;
	L4_Word_t addr;
	int rights;
	void (*finish)(Pagefault *fault);
};

// ELF loading
typedef struct ElfloadRequest_t {
	char path[MAX_FILE_NAME]; // path to executable
	pid_t parent;             // pid of the process that made the request
	pid_t child;              // pid of the process created
} ElfloadRequest;

static L4_ThreadId_t virtualPager; // automatically L4_nilthread
static void pager_handler(void);

// For copyin/copyout
#define LO_HALF_MASK 0x0000ffff
#define LO_HALF(word) ((word) & 0x0000ffff)
#define HI_HALF_MASK 0xffff0000
#define HI_HALF(word) (((word) >> 16) & 0x0000ffff)
#define HI_SHIFT(word) ((word) << 16)

static L4_Word_t copyInOutData[MAX_THREADS];
static char copyInOutBuffer[MAX_THREADS * MAX_IO_BUF];

static void copyIn(L4_ThreadId_t tid, void *src, size_t size, int append);
static void copyOut(L4_ThreadId_t tid, void *dest, size_t size, int append);

typedef struct Pagetable2_t {
	L4_Word_t pages[PAGEWORDS];
} Pagetable2;

typedef struct Pagetable1_t {
	Pagetable2 *pages2[PAGEWORDS];
} Pagetable1;

Pagetable *pagetable_init(void) {
	assert(sizeof(Pagetable1) == PAGESIZE);
	Pagetable1 *pt = (Pagetable1*) frame_alloc(FA_PAGETABLE1);

	for (int i = 0; i < PAGEWORDS; i++) {
		pt->pages2[i] = NULL;
	}

	return (Pagetable*) pt;
}

L4_ThreadId_t pager_get_tid(void) {
	return virtualPager;
}

int pager_is_active(void) {
	return !L4_IsThreadEqual(virtualPager, L4_nilthread);
}

static void pagedump(char *addr) {
	(void) pagedump;
	const int PER_ROW = 32;
	for (int row = 0; row < PAGESIZE; row += PER_ROW) {
		printf("%04x:", row);
		for (int col = 0; col < PER_ROW; col += 2) {
			printf(" %02x", 0x000000ff & *(addr++));
			printf("%02x", 0x000000ff & *(addr++));
		}
		printf("\n");
	}
}

static L4_Word_t* pagetableLookup(Pagetable *pt, L4_Word_t addr) {
	Pagetable1 *level1 = (Pagetable1*) pt;

	addr /= PAGESIZE;
	int offset1 = addr / PAGEWORDS;
	int offset2 = addr - (offset1 * PAGEWORDS);

	if (level1 == NULL) {
		dprintf(0, "!!! pagetableLookup: level1 is NULL!\n");
		return NULL;
	} else if (level1->pages2 == NULL) {
		dprintf(0, "!!! pagetableLookup: level1->pages2 is NULL!\n");
		return NULL;
	}

	if (level1->pages2[offset1] == NULL) {
		assert(sizeof(Pagetable2) == PAGESIZE);
		level1->pages2[offset1] = (Pagetable2*) frame_alloc(FA_PAGETABLE2);

		for (int i = 0; i < PAGEWORDS; i++) {
			level1->pages2[offset1]->pages[i] = 0;
		}
	}

	return &level1->pages2[offset1]->pages[offset2];
}

static void pagetableFree(Process *p) {
	assert(p != NULL);
	Pagetable1 *pt1 = (Pagetable1*) process_get_pagetable(p);
	assert(pt1 != NULL);

	for (int i = 0; i < PAGEWORDS; i++) {
		if (pt1->pages2[i] != NULL) {
			frame_free((L4_Word_t) pt1->pages2[i]);
		}
	}

	frame_free((L4_Word_t) pt1);
}

static void pagerFrameFree(Process *p, L4_Word_t frame) {
	assert((frame & ~PAGEALIGN) == 0);
	frame_free(frame);
	allocLimit++;

	if (p != NULL) process_get_info(p)->size--;
}

static int framesFree(void *contents, void *data) {
	Pair *curr = (Pair*) contents; // (pid, word)
	Process *p = (Process*) data;

	if ((pid_t) curr->fst == process_get_pid(p)) {
		L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), curr->snd);
		pagerFrameFree(p, *entry & ADDRESS_MASK);
		return 1;
	} else {
		return 0;
	}
}

static int mmappedFree(void *contents, void *data) {
	MMap *curr = (MMap*) contents;
	Process *p = (Process*) data;
	return (curr->pid == process_get_pid(p));
}

static int mapPage(
		L4_SpaceId_t sid, L4_Word_t virt, L4_Word_t phys, int rights) {
	assert((virt & ~PAGEALIGN) == 0);
	assert((phys & ~PAGEALIGN) == 0);

	L4_Fpage_t fpage = L4_Fpage(virt, PAGESIZE);
	L4_Set_Rights(&fpage, rights);
	L4_PhysDesc_t ppage = L4_PhysDesc(phys, DEFAULT_MEMORY);

	int result = L4_MapFpage(sid, fpage, ppage);
	please(result);
	return result;
}

static int unmapPage(L4_SpaceId_t sid, L4_Word_t virt) {
	assert((virt & ~PAGEALIGN) == 0);

	L4_Fpage_t fpage = L4_Fpage(virt, PAGESIZE);
	int result = L4_UnmapFpage(sid, fpage);

	if (!result) {
		dprintf(0, "!!! unmapPage failed: ");
		sos_print_error(L4_ErrorCode());
	}

	return result;
}

static void prepareDataIn(Process *p, L4_Word_t vaddr) {
	// Prepare for some data from a user program to be fiddled with by
	// the pager.  This involves flushing the user programs cache on this
	// address, and invalidating our own cache.
	assert((vaddr & ~PAGEALIGN) == 0);

	L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), vaddr);
	L4_Word_t frame = *entry & ADDRESS_MASK;

	dprintf(3, "*** prepareDataIn: p=%d vaddr=%p frame=%p\n",
			process_get_pid(p), (void*) vaddr, (void*) frame);

	please(CACHE_FLUSH_RANGE(process_get_sid(p), vaddr, vaddr + PAGESIZE));
	please(CACHE_FLUSH_RANGE_INVALIDATE(L4_rootspace, frame, frame + PAGESIZE));
}

static void prepareDataOut(Process *p, L4_Word_t vaddr) {
	// Prepare from some data that has been changed in here to be reflected
	// on the user space.  This involves flushing our own cache, then
	// invalidating the user's cache on the address.
	assert((vaddr & ~PAGEALIGN) == 0);

	L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), vaddr);
	L4_Word_t frame = *entry & ADDRESS_MASK;

	dprintf(3, "*** prepareDataOut: p=%d vaddr=%p frame=%p\n",
			process_get_pid(p), (void*) vaddr, (void*) frame);

	please(CACHE_FLUSH_RANGE(L4_rootspace, frame, frame + PAGESIZE));
	please(CACHE_FLUSH_RANGE_INVALIDATE(
				process_get_sid(p), vaddr, vaddr + PAGESIZE));
}

static Pair *deleteAllocList(void) {
	dprintf(1, "*** deleteAllocList\n");

	assert(!list_null(alloced));

	Process *p;
	Pair *found = NULL; // (pid, word)
	L4_Word_t *entry;

	// Second-chance algorithm
	for (;;) {
		found = (Pair*) list_unshift(alloced);

		p = process_lookup(found->fst);
		assert(p != NULL);
		entry = pagetableLookup(process_get_pagetable(p), found->snd);

		dprintf(3, "*** deleteAllocList: p=%d page=%p frame=%p\n",
				process_get_pid(p), (void*) found->snd, 
				(void*) (*entry & ADDRESS_MASK));

		//if (((*entry & REF_MASK) == 0) && (found->snd < 0x70000000)) {
		if ((*entry & REF_MASK) == 0) {
			// Not been referenced, this is the frame to swap
			break;
		} else {
			// Been referenced: clear refbit, unmap to give it a chance
			// of being reset again, and move to back
			*entry &= ~REF_MASK;
			unmapPage(process_get_sid(p), found->snd);
			list_push(alloced, found);
		}
	}

	return found;
}

static L4_Word_t pagerFrameAlloc(Process *p, L4_Word_t page) {
	L4_Word_t frame;

	assert(allocLimit >= 0);

	if (allocLimit == 0) {
		dprintf(1, "*** pagerFrameAlloc: allocLimit reached\n");
		frame = 0;
	} else {
		frame = frame_alloc(FA_PAGERALLOC);
		dprintf(1, "*** pagerFrameAlloc: allocated frame %p\n", frame);
		list_push(alloced, pair_alloc(process_get_pid(p), page));

		process_get_info(p)->size++;
		allocLimit--;
	}

	return frame;
}

void pager_init(void) {
	// Set up lists
	allocLimit = FRAME_ALLOC_LIMIT;
	alloced = list_empty();
	swapped = list_empty();
	mmapped = list_empty();
	requests = list_empty();

	// The default swapfile (.swap)
	defaultSwapfile = swapfile_init(SWAPFILE_FN);

	// Start the real pager process
	Process *p = process_run_rootthread("pager", pager_handler,
			YES_TIMESTAMP);
	process_set_ipcfilt(p, PS_IPC_NONBLOCKING);

	// Wait until it has actually started
	while (!pager_is_active()) L4_Yield();
}

static int findHeap(void *contents, void *data) {
	return (region_get_type((Region*) contents) == REGION_HEAP);
}

static int heapGrow(uintptr_t *base, unsigned int nb) {
	dprintf(2, "*** heapGrow(%p, %lx)\n", base, nb);

	// Find the current heap section.
	Process *p = process_lookup(L4_SpaceNo(L4_SenderSpace()));
	Region *heap = list_find(process_get_regions(p), findHeap, NULL);
	assert(heap != NULL);

	// Top of heap is the (new) start of the free region, this is
	// what morecore/malloc expect.
	dprintf(2, "*** %s: base was %p, now ", __FUNCTION__, (void*) *base);
	*base = region_get_base(heap) + region_get_size(heap);
	dprintf(2, "%p\n", (void*) *base);

	// Move the heap region so SOS knows about it.
	region_set_size(heap, nb + region_get_size(heap));

	// Have the option of returning 0 to signify no more memory.
	return 1;
}

static int memoryUsage(void) {
	return FRAME_ALLOC_LIMIT - allocLimit;
}

static int findRegion(void *contents, void *data) {
	Region *r = (Region*) contents;
	L4_Word_t addr = (L4_Word_t) data;

	return ((addr >= region_get_base(r)) &&
			(addr < region_get_base(r) + region_get_size(r)));
}

static ElfloadRequest *allocElfload(char *path, pid_t caller, pid_t child) {
	ElfloadRequest *er = (ElfloadRequest *) malloc(sizeof(ElfloadRequest));

	strncpy(er->path, path, MAX_FILE_NAME);
	er->parent = caller;
	er->child = child;

	return er;
}

static MMap *allocMMap(pid_t pid, L4_Word_t memAddr, L4_Word_t dskAddr,
		size_t size, int rights, char *path) {
	MMap *mmap = (MMap*) malloc(sizeof(MMap));

	mmap->pid = pid;
	mmap->memAddr = memAddr;
	mmap->dskAddr = dskAddr;
	mmap->size = size;
	mmap->rights = rights;
	strncpy(mmap->path, path, MAX_FILE_NAME);

	printf("dont forget to free MMap too\n");

	return mmap;
}

static void pagefaultFinish(Pagefault *fault) {
	dprintf(2, "*** %s: finished with %d\n", __FUNCTION__, fault->pid);

	L4_ThreadId_t tid = process_get_tid(process_lookup(fault->pid));
	free(fault);
	syscall_reply_v(tid, 0);
}

static L4_Word_t pagerSwapslotAlloc(Process *p) {
	L4_Word_t diskAddr = swapslot_alloc(defaultSwapfile);

	if (diskAddr == ADDRESS_NONE) {
		dprintf(0, "!!! pagerSwapslotAlloc: none available\n");
		return ADDRESS_NONE;
	} else {
		list_push(swapped, pair_alloc(process_get_pid(p), diskAddr));
		return diskAddr;
	}
}

static int pagerSwapslotFree(void *contents, void *data) {
	Pair *curr = (Pair*) contents; // (pid, word)
	Pair *args = (Pair*) data;     // (pid, word)

	if ((curr->fst == args->fst) &&
			((curr->snd == args->snd) || (curr->snd == ADDRESS_ALL))) {
		swapslot_free(defaultSwapfile, curr->snd);
		return 1;
	} else {
		return 0;
	}
}

static void regionsFree(void *contents, void *data) {
	region_free((Region*) contents);
}

static int processDelete(L4_Word_t pid) {
	Process *p;
	Pair args; // (pid, word)

	p = process_lookup(pid);

	if (p == NULL) {
		// Already killed?
		return 1;
	}
	
	if (process_kill(p) != 0) {
		// Invalid process
		return (-1);
	}
	
	// change the state
	process_set_state(p, PS_STATE_ZOMBIE);

	// flush and close open files
	process_close_files(p);
	process_remove(p);

	// Free all resources
	args = PAIR(process_get_pid(p), ADDRESS_ALL);
	list_delete(alloced, framesFree, p);
	list_delete(swapped, pagerSwapslotFree, &args);
	list_delete(mmapped, mmappedFree, &args);
	pagetableFree(p);
	list_iterate(process_get_regions(p), regionsFree, NULL);

	// Wake all waiting processes
	process_wake_all(process_get_pid(p));

	// And done
	free(p);

	return 0;
}

// Handle a page fault.  Will return the type of request needed to complete
// the page fault, if any.
static rtype_t pagefaultHandle(Pagefault *fault) {
	Process *p;
	L4_Word_t frame, *entry;

	dprintf(2, "*** %s: fault on pid=%d, addr=%p rights=%d\n",
			__FUNCTION__, fault->pid, fault->addr, fault->rights);

	p = process_lookup(fault->pid);
	if (p == NULL) {
		// Process died
		return REQUEST_NONE;
	}

	// Find region it belongs in.
	dprintf(3, "*** %s: finding region\n", __FUNCTION__);
	Region *r = list_find(
			process_get_regions(p), findRegion, (void*) fault->addr);

	if (r == NULL) {
		printf("Segmentation fault (%d)\n", process_get_pid(p));
		processDelete(process_get_pid(p));
		return REQUEST_NONE;
	}

	// TODO check region rights against the fault rights

	// Place in, or retrieve from, page table.
	dprintf(3, "*** %s: finding entry\n", __FUNCTION__);
	entry = pagetableLookup(process_get_pagetable(p), fault->addr);
	frame = *entry & ADDRESS_MASK;
	dprintf(3, "*** %s: found entry %p\n", __FUNCTION__, (void*) *entry);

	if (*entry & SWAP_MASK) {
		dprintf(2, "*** %s: page %p swapped\n", __FUNCTION__, (void*) *entry);
		return REQUEST_SWAPIN;
	} else if (*entry & MMAP_MASK) {
		dprintf(2, "*** %s: page %p mmapped\n", __FUNCTION__, (void*) *entry);
		return REQUEST_MMAP_READ;
	} else if ((frame != 0) && ((*entry & TEMP_MASK) == 0)) {
		dprintf(3, "*** %s: page %p unmapped\n", __FUNCTION__, (void*) *entry);
	} else if (region_map_directly(r)) {
		dprintf(2, "*** %s: mapping directly\n", __FUNCTION__); // bootinfo
		frame = fault->addr & PAGEALIGN;
	} else {
		dprintf(2, "*** %s: allocating frame\n", __FUNCTION__);

		frame = pagerFrameAlloc(p, fault->addr & PAGEALIGN);
		assert((frame & ~ADDRESS_MASK) == 0); // no flags set

		if (frame == 0) {
			dprintf(2, "*** %s: no free frames\n", __FUNCTION__);
			return REQUEST_PAGEFAULT;
		}
	}

	// If it was faulting due to the frame only being temporary, copy it across
	if ((*entry & TEMP_MASK) != 0) {
		dprintf(2, "*** %s: %p was only temporary\n", __FUNCTION__, *entry);
		memcpy((void*) frame, (void*) (*entry & ADDRESS_MASK), PAGESIZE);
		unmapPage(process_get_sid(p), *entry & ADDRESS_MASK);
		frame_free(*entry & ADDRESS_MASK);

		*entry = frame | REF_MASK;
		prepareDataOut(p, fault->addr & PAGEALIGN);
	} else {
		*entry = frame | REF_MASK;
	}

	dprintf(3, "*** %s: mapping vaddr=%p pid=%d frame=%p rights=%d\n",
			__FUNCTION__, (void*) (fault->addr & PAGEALIGN), process_get_pid(p),
			(void*) frame, region_get_rights(r));

	mapPage(process_get_sid(p), fault->addr & PAGEALIGN, frame,
			region_get_rights(r));

	return REQUEST_NONE;
}

static void panic2(int success) {
	assert(!"panic");
}

static Request *allocRequest(rtype_t type, void *data) {
	Request *req = (Request*) malloc(sizeof(Request));

	req->type = type;
	req->data = data;
	req->stage = 0;
	req->finish = panic2;

	return req;
}

static ReadRequest *allocReadRequest(
		L4_Word_t dst, L4_Word_t src, int size, char *path) {
	ReadRequest *readReq = (ReadRequest*) malloc(sizeof(ReadRequest));

	readReq->dst = dst;
	readReq->src = src;
	readReq->size = size;
	readReq->offset = 0;
	strncpy(readReq->path, path, MAX_FILE_NAME);
	readReq->fd = VFS_NIL_FILE;
	readReq->rights = FM_READ | FM_WRITE;
	readReq->alwaysOpen = FALSE;

	return readReq;
}

static void pager2(Pagefault *fault) {
	dprintf(1, "*** %s\n", __FUNCTION__);
	rtype_t requestNeeded = pagefaultHandle(fault);

	if (requestNeeded == REQUEST_NONE) {
		dprintf(3, "*** %s: finished request\n", __FUNCTION__);
		fault->finish(fault);
	} else {
		dprintf(2, "*** %s: delay request\n", __FUNCTION__);
		queueRequest2(allocRequest(REQUEST_PAGEFAULT, fault));
	}
}

static void setRegionMMap(Process *p, Region *r, L4_Word_t diskAddr,
		size_t diskSize, char *path) {
	assert((diskAddr & ~PAGEALIGN) == 0);
	L4_Word_t *entry;
	L4_Word_t base = region_get_base(r);

	for (size_t size = 0; size < region_get_size(r); size += PAGESIZE) {
		entry = pagetableLookup(process_get_pagetable(p), base + size);
		*entry = (diskAddr + size) | MMAP_MASK;
		list_push(mmapped, allocMMap(process_get_pid(p), base + size,
					diskAddr + size, min(PAGESIZE, diskSize - size),
					region_get_rights(r), path));
	}
}

static char *wordAlign(char *s) {
	unsigned int x = (unsigned int) s;
	x--;
	x += sizeof(L4_Word_t) - (x % sizeof(L4_Word_t));
	return (char*) x;
}

static void finishElfload(int success) {
	dprintf(2, "*** %s: success = %d\n", __FUNCTION__, success);

	Request *req;
	ReadRequest *readReq;
	ElfloadRequest *elfReq;
	struct Elf32_Header *header;
	Process *p;
	L4_ThreadId_t parent;
	L4_Word_t diskAddr;

	req = (Request*) list_peek(requests);
	assert(req->type == REQUEST_READ);
	readReq = (ReadRequest*) req->data;
	list_unshift(requests);

	assert(((Request*) list_peek(requests))->type == REQUEST_ELFLOAD);
	elfReq = (ElfloadRequest*) ((Request*) list_peek(requests))->data;
	header = (struct Elf32_Header*) readReq->dst;
	parent = process_get_tid(process_lookup(elfReq->parent));

	if (!success) {
		dprintf(1, "*** %s: failed\n", __FUNCTION__);
		syscall_reply(parent, -1);
	} else if (elf32_checkFile(header) != 0) {
		dprintf(1, "*** %s: not an ELF file\n", __FUNCTION__);
		syscall_reply(parent, -1);
	} else {
		p = process_init(PS_TYPE_PROCESS);
		process_set_name(p, elfReq->path);
		process_get_info(p)->pid = elfReq->child;

		for (int i = 0; i < elf32_getNumProgramHeaders(header); i++) {
			Region *r = region_alloc(
					REGION_OTHER,
					elf32_getProgramHeaderVaddr(header, i),
					elf32_getProgramHeaderMemorySize(header, i),
					elf32_getProgramHeaderFlags(header, i), 0);
			diskAddr = elf32_getProgramHeaderOffset(header, i) & PAGEALIGN;
			process_add_region(p, r);
			setRegionMMap(p, r, diskAddr,
					elf32_getProgramHeaderFileSize(header, i), elfReq->path);
		}

		process_prepare(p);
		process_set_ip(p, (void*) elf32_getEntryPoint(header));
		if (verbose > 1) process_dump(p);
		process_run(p, YES_TIMESTAMP);

		syscall_reply(parent, process_get_pid(p));
	}

	free(req);
	frame_free(readReq->dst);
	free(readReq);
	free(elfReq);
	dequeueRequest2();
}

static void startElfload(void) {
	Request *req;
	ReadRequest *readReq;
	ElfloadRequest *elfReq;

	assert(((Request*) list_peek(requests))->type == REQUEST_ELFLOAD);
	elfReq = (ElfloadRequest*) ((Request*) list_peek(requests))->data;

	// MAX_IO_BUF because we don't really know the size yet (but it will be
	// less than a kilobyte!) and frame_alloc for temp storage (must free)
	readReq = allocReadRequest(frame_alloc(FA_PAGERALLOC),
			0, MAX_IO_BUF, elfReq->path);
	readReq->rights = FM_READ | FM_EXEC;
	req = allocRequest(REQUEST_READ, readReq);
	req->finish = finishElfload;

	list_shift(requests, req);
	startRequest2();
}

static void printRequests2(void *contents, void *data) {
	Request *req = (Request*) contents;

	printf("type: %d, ", req->type);

	switch (req->type) {
		case REQUEST_NONE:
		case REQUEST_SWAPOUT:
		case REQUEST_SWAPIN:
		case REQUEST_ELFLOAD:
		case REQUEST_MMAP_READ:
			assert(0);
			break;

		case REQUEST_PAGEFAULT:
		case REQUEST_WRITE:
		case REQUEST_READ:
			break;

		default:
			assert(! __FUNCTION__);
	}

	printf("\n");
}

static void dequeueRequest2(void) {
	dprintf(1, "*** %s\n", __FUNCTION__);

	printf("WARNING make sure data is freed\n");
	free(list_unshift(requests));

	if (list_null(requests)) {
		dprintf(1, "*** %s: no more items\n", __FUNCTION__);
	} else {
		dprintf(1, "*** %s: running next\n", __FUNCTION__);
		if (verbose > 2) list_iterate(requests, printRequests2, NULL);
		startRequest2();
	}
}

static void queueRequest2(Request *req) {
	queueRequests(1, req);
}

static void queueRequests(int n, ...) {
	if (verbose > 2) list_iterate(requests, printRequests2, NULL);
	int startImmediately = list_null(requests);

	va_list va;
	va_start(va, n);

	for (int i = 0; i < n; i++) {
		list_push(requests, va_arg(va, Request*));
	}

	va_end(va);

	if (startImmediately) {
		startRequest2();
	}
}

static void finishSwapout2(int success) {
	assert(success);
	dprintf(1, "*** %s\n", __FUNCTION__);

	Request *req = (Request*) list_peek(requests);
	assert(req->type == REQUEST_WRITE);
	WriteRequest *writeReq = (WriteRequest*) req->data;

	pagerFrameFree(process_lookup(writeReq->pid), writeReq->src);

	free(writeReq);
	dequeueRequest2();
}

static void finishSwapin2(int success) {
	assert(success);
	dprintf(1, "*** %s\n", __FUNCTION__);

	Request *req = (Request*) list_peek(requests);
	assert(req->type == REQUEST_READ);
	ReadRequest *readReq = (ReadRequest*) req->data;

	free(readReq);
	dequeueRequest2();
}

/*
static void finishSwapelf(void) {
	PagerRequest *pr = (PagerRequest*) requestsPeek();
	Process *p = process_lookup(pr->pid);
	Region *r = list_find(process_get_regions(p), findRegion, (void*) pr->addr);
	L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), pr->addr);

	// Zero the area between the end of memory (i.e. the end of the region) and
	// the end of the file (i.e. region_get_filesize)
	L4_Word_t fileTop = region_get_base(r) + region_get_filesize(r);

	if ((fileTop & PAGEALIGN) == (pr->addr & PAGEALIGN)) {
		dprintf(2, "*** finishSwapelf: zeroing from %p because of addr %p\n",
				(void*) fileTop, (void*) pr->addr);
		assert(region_get_size(r) >= region_get_filesize(r));
		memset((char*) swapinFrame + (fileTop % PAGESIZE), 0x00,
				region_get_size(r) - region_get_filesize(r));
	}

	// Page no longer on ELF file.  Yay.
	*entry &= ~ELF_MASK;

	// I'm 99.99% sure it's ok to do this without any other magic
	finishSwapin();
}
*/

static void continueRead(int vfsRval) {
	dprintf(2, "*** %s: vfsRval=%d\n", __FUNCTION__, vfsRval);
	Request *req = (Request*) list_peek(requests);
	assert(req->type == REQUEST_READ);
	ReadRequest *readReq = (ReadRequest*) req->data;
	stat_t *readStat;
	char *buf;

	switch (req->stage) {
		case STAGE_OPEN:
			dprintf(2, "*** %s: STAGE_OPEN\n", __FUNCTION__);
			if (vfsRval < 0) {
				req->finish(FALSE);
			} else {
				req->stage++;
				readReq->fd = vfsRval;
				readReq->offset = 0;
				strncpy(pager_buffer(sos_my_tid()), readReq->path, MAX_FILE_NAME);
				statNonblocking();
			}

			break;

		case STAGE_STAT:
			dprintf(2, "*** %s: STAGE_STAT\n", __FUNCTION__);
			assert(vfsRval == 0);

			buf = pager_buffer(sos_my_tid());
			readStat = (stat_t*) wordAlign(buf + strlen(buf) + 1);

			printf("%d vs %d\n", readReq->rights, readStat->st_fmode);

			/*
			if ((readReq->rights & readStat->st_fmode) == 0) {
				req->finish(FALSE);
			} else {
			*/
				dprintf(2, "*** %s: seek %p\n", __FUNCTION__, (void*) readReq->src);
				req->stage++;
				lseekNonblocking(readReq->fd, readReq->src, SEEK_SET);
			//}

			break;

		case STAGE_LSEEK:
			dprintf(2, "*** %s: STAGE_LSEEK\n", __FUNCTION__);
			assert(vfsRval == 0);
			req->stage++;
			readNonblocking(readReq->fd, min(IO_MAX_BUFFER, readReq->size));

			break;

		case STAGE_READ:
			dprintf(2, "*** %s: STAGE_READ\n", __FUNCTION__);
			assert(vfsRval >= 0);
			memcpy((void*) (readReq->dst + readReq->offset),
					pager_buffer(sos_my_tid()), vfsRval);
			readReq->offset += vfsRval;
			assert(readReq->offset <= readReq->size);

			if (readReq->offset == readReq->size) {
				if (readReq->alwaysOpen) {
					req->finish(TRUE);
				} else {
					req->stage++;
					closeNonblocking(readReq->fd);
				}
			} else {
				assert(vfsRval > 0);
				readNonblocking(readReq->fd,
						min(IO_MAX_BUFFER, readReq->size - readReq->offset));
			}

			break;

		case STAGE_CLOSE:
			dprintf(2, "*** %s: STAGE_CLOSE\n", __FUNCTION__);
			req->finish(TRUE);

			break;

		default:
			assert(! __FUNCTION__);
	}
}

static void startRead(void) {
	Request *req = (Request*) list_peek(requests);
	assert(req->type == REQUEST_READ);
	ReadRequest *readReq = (ReadRequest*) req->data;

	dprintf(1, "*** startRead: path=\"%s\"\n", readReq->path);
	fildes_t currentFd = vfs_getfd(L4_ThreadNo(sos_my_tid()), readReq->path);

	if (currentFd != VFS_NIL_FILE) {
		assert(readReq->alwaysOpen);
		dprintf(2, "%s: open at %d\n", __FUNCTION__, currentFd);
		req->stage = STAGE_OPEN + 1;
		readReq->fd = currentFd;
		lseekNonblocking(readReq->fd, readReq->src, SEEK_SET);
	} else {
		dprintf(2, "%s: not open\n", __FUNCTION__);
		req->stage = STAGE_OPEN;
		strncpy(pager_buffer(sos_my_tid()), readReq->path, MAX_IO_BUF);
		//openNonblocking(NULL, readReq->rights);
		openNonblocking(NULL, FM_READ);
	}
}

static void continueWrite(int vfsRval) {
	dprintf(2, "*** %s, vfsRval=%d\n", __FUNCTION__, vfsRval);
	Request *req = (Request*) list_peek(requests);
	assert(req->type == REQUEST_WRITE);
	WriteRequest *writeReq = (WriteRequest*) req->data;
	stat_t *writeStat;
	char *buf;
	size_t size;

	switch (req->stage) {
		case STAGE_OPEN:
			dprintf(2, "*** %s: STAGE_OPEN\n", __FUNCTION__);
			if (vfsRval < 0) {
				req->finish(FALSE);
			} else {
				req->stage++;
				writeReq->fd = vfsRval;
				dprintf(2, "*** %s: seek %p\n", __FUNCTION__, (void*) writeReq->dst);
				memcpy(pager_buffer(sos_my_tid()), writeReq->path, MAX_FILE_NAME);
				statNonblocking();
			}

			break;

		case STAGE_STAT:
			dprintf(2, "*** %s: STAGE_STAT\n", __FUNCTION__);
			assert(vfsRval == 0);

			buf = pager_buffer(sos_my_tid());
			writeStat = (stat_t*) wordAlign(buf + strlen(buf) + 1);

			printf("%d vs %d\n", writeReq->rights, writeStat->st_fmode);

			/*
			if ((writeReq->rights & writeStat->st_fmode) == 0) {
				req->finish(FALSE);
			} else {
			*/
				dprintf(2, "*** %s: seek %p\n", __FUNCTION__, (void*) writeReq->src);
				req->stage++;
				lseekNonblocking(writeReq->fd, writeReq->dst, SEEK_SET);
			//}

			break;

		case STAGE_LSEEK:
			dprintf(2, "*** %s: STAGE_LSEEK\n", __FUNCTION__);
			assert(vfsRval == 0);
			req->stage++;
			size = min(IO_MAX_BUFFER, writeReq->size);
			memcpy(pager_buffer(sos_my_tid()), (void*) writeReq->src, size);
			writeNonblocking(writeReq->fd, size);

			break;

		case STAGE_WRITE:
			dprintf(2, "*** %s: STAGE_WRITE\n", __FUNCTION__);
			assert(vfsRval >= 0);
			writeReq->offset += vfsRval;
			assert(writeReq->offset <= writeReq->size);

			if (writeReq->offset == writeReq->size) {
				if (writeReq->alwaysOpen) {
					req->finish(TRUE);
				} else {
					req->stage++;
					closeNonblocking(writeReq->fd);
				}
			} else {
				assert(vfsRval > 0);
				size = min(IO_MAX_BUFFER, writeReq->size - writeReq->offset);
				memcpy(pager_buffer(sos_my_tid()),
						(void*) (writeReq->src + writeReq->offset), size);
				writeNonblocking(writeReq->fd, size);
			}

			break;

		case STAGE_CLOSE:
			dprintf(2, "*** %s: STAGE_CLOSE\n", __FUNCTION__);
			req->finish(TRUE);

			break;
	}
}

static void startWrite(void) {
	Request *req = (Request*) list_peek(requests);
	assert(req->type == REQUEST_WRITE);
	WriteRequest *writeReq = (WriteRequest*) req->data;

	dprintf(1, "*** %s: path=\"%s\"\n", __FUNCTION__, writeReq->path);
	fildes_t currentFd = vfs_getfd(L4_ThreadNo(sos_my_tid()), writeReq->path);

	if (currentFd != VFS_NIL_FILE) {
		assert(writeReq->alwaysOpen);
		dprintf(2, "%s: open at %d\n", __FUNCTION__, currentFd);
		req->stage = STAGE_OPEN + 1;
		writeReq->fd = currentFd;
		lseekNonblocking(writeReq->fd, writeReq->dst, SEEK_SET);
	} else {
		dprintf(2, "%s: not open\n", __FUNCTION__);
		req->stage = STAGE_OPEN;
		strncpy(pager_buffer(sos_my_tid()), writeReq->path, MAX_IO_BUF);
		openNonblocking(NULL, writeReq->rights);
	}
}

static int findMMap(void *contents, void *data) {
	MMap *mmap = (MMap*) contents;
	Pair *pair = (Pair*) data; // (pid, word)

	printf("mmap: pid=%d memAddr=%p dskAddr=%p size=%d rights=%d path=%s\n",
			mmap->pid, (void*) mmap->memAddr, (void*) mmap->dskAddr,
			mmap->size, mmap->rights, mmap->path);

	if ((mmap->pid == pair->fst) && (pair->snd >= mmap->memAddr) &&
			(pair->snd < (mmap->memAddr + PAGESIZE))) {
		return 1;
	} else {
		return 0;
	}
}

static void finishMMapRead(int success) {
	assert(success);
	dprintf(1, "*** %s\n", __FUNCTION__);

	Request *req = (Request*) list_peek(requests);
	assert(req->type == REQUEST_READ);
	ReadRequest *readReq = (ReadRequest*) req->data;

	free(readReq);
	dequeueRequest2();
}

static void startMMapRead(void) {
	dprintf(2, "*** startMMapRead\n");

	Process *p;
	Request *faultReq, *req;
	Pagefault *fault;
	ReadRequest *readReq;
	Pair args;
	L4_Word_t *entry;
	L4_Word_t frame;
	MMap *mmap;

	// The data for the mmap comes from the head of the queue
	faultReq = (Request*) list_peek(requests);
	assert(faultReq->type == REQUEST_PAGEFAULT);
	fault = (Pagefault*) faultReq->data;

	p = process_lookup(fault->pid);

	// Find the actual mmap
	args = PAIR(process_get_pid(p), fault->addr);
	mmap = (MMap*) list_find(mmapped, findMMap, &args);
	assert(mmap != NULL);

	// Reserve a temporary frame to read in to
	frame = frame_alloc(FA_PAGERALLOC);
	dprintf(2, "*** %s: temporary frame is %p\n", __FUNCTION__, (void*) frame);
	memset((void*) frame, 0x00, PAGESIZE);

	// Set up the read request
	entry = pagetableLookup(process_get_pagetable(p), fault->addr);

	dprintf(2, "*** %s: reading from %p in to %p\n", __FUNCTION__,
			(void*) (*entry & ADDRESS_MASK), (void*) frame);

	readReq = allocReadRequest(frame, *entry & ADDRESS_MASK,
			mmap->size, mmap->path);
	readReq->rights = mmap->rights;
	readReq->alwaysOpen = (strcmp(mmap->path, SWAPFILE_FN) == 0); // hack
	req = allocRequest(REQUEST_READ, readReq);
	req->finish = finishMMapRead;

	// Free the swapslot
	if (strcmp(mmap->path, SWAPFILE_FN) == 0) {
		args = PAIR(process_get_pid(p), *entry & ADDRESS_MASK);
		list_delete(swapped, pagerSwapslotFree, &args);
	}

	// Update page table with temporary frame etc
	dprintf(2, "*** %s: entry was %p, now ", __FUNCTION__, *entry);
	*entry &= ~(ADDRESS_MASK | MMAP_MASK);
	*entry |= frame | TEMP_MASK;
	dprintf(2, "%p\n", *entry);

	// Push the swapin to the head of the queue
	list_shift(requests, req);
	startRequest2();
}

static void startSwapin2(void) {
	dprintf(2, "*** %s\n", __FUNCTION__);

	Process *p;
	Request *faultReq, *req;
	Pagefault *fault;
	ReadRequest *readReq;
	Pair args;
	L4_Word_t *entry;
	L4_Word_t frame;

	// The data for the swapin comes from the head of the queue
	faultReq = (Request*) list_peek(requests);
	assert(faultReq->type == REQUEST_PAGEFAULT);
	fault = (Pagefault*) faultReq->data;

	p = process_lookup(fault->pid);

	// Reserve a temporary frame to read in to
	frame = frame_alloc(FA_PAGERALLOC);
	dprintf(2, "*** %s: temporary frame is %p\n", __FUNCTION__, (void*) frame);

	// Set up the read request
	entry = pagetableLookup(process_get_pagetable(p), fault->addr);

	dprintf(2, "*** %s: reading from %p in to %p\n", __FUNCTION__,
			(void*) (*entry & ADDRESS_MASK), (void*) frame);

	readReq = allocReadRequest(frame, *entry & ADDRESS_MASK,
			PAGESIZE, SWAPFILE_FN);
	readReq->alwaysOpen = TRUE;
	req = allocRequest(REQUEST_READ, readReq);
	req->finish = finishSwapin2;

	// Free the swapslot
	args = PAIR(process_get_pid(p), *entry & ADDRESS_MASK);
	list_delete(swapped, pagerSwapslotFree, &args);

	// Update page table with temporary frame etc
	dprintf(2, "*** %s: entry was %p, now ", __FUNCTION__, *entry);
	*entry &= ~(ADDRESS_MASK | SWAP_MASK);
	*entry |= frame | TEMP_MASK;
	dprintf(2, "%p\n", *entry);

	// Push the swapin to the head of the queue
	list_shift(requests, req);
	startRequest2();
}

static void startSwapout2(void) {
	dprintf(2, "*** %s\n", __FUNCTION__);

	Process *p;
	Request *req;
	WriteRequest *writeReq;
	Pair *swapout; // (pid, word)
	L4_Word_t *entry, diskAddr, frame;

	// Choose the next page to swap out
	swapout = deleteAllocList();
	p = process_lookup(swapout->fst);
	assert(p != NULL);

	entry = pagetableLookup(process_get_pagetable(p), swapout->snd);
	frame = *entry & ADDRESS_MASK;

	// Fix caches
	assert((swapout->snd & ~PAGEALIGN) == 0);
	prepareDataIn(p, swapout->snd);

	// The page is no longer backed
	unmapPage(process_get_sid(p), swapout->snd);

	// Set up where on disk to put the page
	diskAddr = pagerSwapslotAlloc(p);
	assert((diskAddr & ~PAGEALIGN) == 0);

	dprintf(1, "*** %s: addr=%p for pid=%d was %p, now %p\n", __FUNCTION__,
			(void*) swapout->snd, process_get_pid(p),
			(void*) frame, (void*) diskAddr);

	pair_free(swapout);

	*entry &= ~ADDRESS_MASK;
	*entry |= diskAddr | SWAP_MASK;

	// Make the request
	writeReq = allocWriteRequest(diskAddr, frame, PAGESIZE, SWAPFILE_FN);
	writeReq->pid = process_get_pid(p);
	writeReq->alwaysOpen = TRUE;
	req = allocRequest(REQUEST_WRITE, writeReq);

	req->finish = finishSwapout2;

	list_shift(requests, req);
	startRequest2();
}

static void startPagefault(void) {
	dprintf(2, "*** %s\n", __FUNCTION__);
	Request *req = (Request*) list_peek(requests);
	Pagefault *fault;
	rtype_t requestNeeded = pagefaultHandle((Pagefault*) req->data);

	switch (requestNeeded) {
		case REQUEST_NONE:
			// Can return now
			fault = (Pagefault*) req->data;
			fault->finish(fault);
			dequeueRequest2();
			break;

		case REQUEST_PAGEFAULT:
			// Need to swap something out
			startSwapout2();
			break;

		case REQUEST_SWAPIN:
			// Need to swap something in
			startSwapin2();
			break;

		case REQUEST_MMAP_READ:
			// Need to read something from disk
			startMMapRead();
			break;

		default:
			assert(!"default");
	}
}

static void startRequest2(void) {
	dprintf(1, "*** %s\n", __FUNCTION__);
	assert(!list_null(requests));

	switch (((Request*) list_peek(requests))->type) {
		case REQUEST_NONE:
			assert(!"REQUEST_NONE");
			break;

		case REQUEST_PAGEFAULT:
			startPagefault();
			break;

		case REQUEST_SWAPOUT:
			assert(!"REQUEST_SWAPOUT");
			break;

		case REQUEST_SWAPIN:
			assert(!"REQUEST_SWAPIN");
			break;

		case REQUEST_MMAP_READ:
			assert(!"REQUEST_MMAP_READ");
			break;

		case REQUEST_ELFLOAD:
			startElfload();
			break;

		case REQUEST_READ:
			startRead();
			break;

		case REQUEST_WRITE:
			startWrite();
			break;

		default:
			assert(!"default");
	}
}

// Continue the current request
static void continueRequest(int rval) {
	Request *req = (Request*) list_peek(requests);
	int newStage = req->cont(req->data, req->stage, rval);

	if (newStage == FINISHED) {
		req->finish(req->data);
		dequeueRequest();
	} else {
		req->stage = newStage;
	}
}

// Start the next request in the queue
static void startRequest(void) {
	Request *req = (Request*) list_peek(requests);
	req->stage = req->init(req->data);
}

// Queue a request and immediately start if at the head
static void queueRequest(void *data, 
		int (*init)(void*), int (*cont)(void*, int), void (*finish)(void *)) {
	Request *req = (Request*) malloc(sizeof(Request));
	req->data = data;
	req->init = init;
	req->cont = cont;
	req->finish = finish;

	if (list_null(requests)) {
		list_push(requests, req);
		startRequest();
	} else {
		list_push(requests, req);
	}
}

// Push a request to the front of the queue and immediately start
static void pushRequest(void) {
	;
}

// Dequeue a request and start the next one if available
static void dequeueRequest(void) {
	free(list_unshift(requests));

	if (!list_null(requests)) {
		startRequest();
	}
}

static void vfsHandler(int vfsRval) {
	dprintf(2, "*** vfsHandler: vfsRval=%d\n", vfsRval);

	switch (((Request*) list_peek(requests))->type) {
		case REQUEST_READ:
			continueRead(vfsRval);
			break;

		case REQUEST_WRITE:
			continueWrite(vfsRval);
			break;

		default:
			assert(!"default");
	}
}

static void pagerFlush(void) {
	if (!L4_UnmapFpage(L4_SenderSpace(), L4_CompleteAddressSpace)) {
		sos_print_error(L4_ErrorCode());
		printf("!!! pager_flush: failed to unmap complete address space\n");
	}
}

static Pagefault *allocPagefault(pid_t pid, L4_Word_t addr, int rights,
		void (*finish)(Pagefault *fault)) {
	Pagefault *fault = (Pagefault*) malloc(sizeof(Pagefault));

	fault->pid = pid;
	fault->addr = addr;
	fault->rights = rights;
	fault->finish = finish;

	return fault;
}

static void pager_handler(void) {
	L4_Accept(L4_AddAcceptor(L4_UntypedWordsAcceptor, L4_NotifyMsgAcceptor));

	virtualPager = sos_my_tid();
	dprintf(1, "*** %s: tid=%ld\n", __FUNCTION__, L4_ThreadNo(virtualPager));

	L4_Msg_t msg;
	L4_MsgTag_t tag;
	L4_ThreadId_t tid = L4_nilthread;
	Process *p;
	ElfloadRequest *elfReq;
	pid_t pid = NIL_PID;
	L4_Word_t tmp;

	for (;;) {
		tag = L4_Wait(&tid);

		tid = sos_sid2tid(L4_SenderSpace());
		p = process_lookup(L4_ThreadNo(tid));
		L4_MsgStore(tag, &msg);

		if (!L4_IsSpaceEqual(L4_SenderSpace(), L4_rootspace) &&
				(TAG_SYSLAB(tag) != SOS_COPYIN) &&
				(TAG_SYSLAB(tag) != SOS_COPYOUT)) {
			dprintf(2, "*** %s: tid=%ld tag=%s\n", __FUNCTION__,
					L4_ThreadNo(tid), syscall_show(TAG_SYSLAB(tag)));
		}

		switch (TAG_SYSLAB(tag)) {
			case L4_PAGEFAULT:
				pager2(allocPagefault(
							process_get_pid(p), L4_MsgWord(&msg, 0),
							L4_Label(tag) & 0x7, pagefaultFinish));
				break;

			case SOS_COPYIN:
				copyIn(tid,
						(void*) L4_MsgWord(&msg, 0),
						(size_t) L4_MsgWord(&msg, 1),
						(int) L4_MsgWord(&msg, 2));
				break;

			case SOS_COPYOUT:
				copyOut(tid,
						(void*) L4_MsgWord(&msg, 0),
						(size_t) L4_MsgWord(&msg, 1),
						(int) L4_MsgWord(&msg, 2));
				break;

			case SOS_REPLY:
				assert(L4_IsSpaceEqual(L4_SenderSpace(), L4_rootspace));
				continueRequest(L4_MsgWord(&msg, 0));
				break;

			case SOS_MOREMEM:
				syscall_reply(tid, heapGrow(
						(uintptr_t*) pager_buffer(tid), L4_MsgWord(&msg, 0)));
				break;

			case SOS_MEMLOC:
				syscall_reply(tid, *(pagetableLookup(process_get_pagetable(p),
								L4_MsgWord(&msg, 0) & PAGEALIGN)));
				break;

			case SOS_MEMUSE:
				syscall_reply(tid, memoryUsage());
				break;

			case SOS_SWAPUSE:
				syscall_reply(tid, swapfile_get_usage(defaultSwapfile));
				break;

			case SOS_PHYSUSE:
				syscall_reply(tid, frames_allocated());
				break;

			case SOS_PROCESS_WAIT:
				tmp = L4_MsgWord(&msg, 0);
				if (tmp == ((L4_Word_t) -1)) {
					process_wait_any(process_lookup(L4_ThreadNo(tid)));
				} else {
					process_wait_for(process_lookup(tmp),
							process_lookup(L4_ThreadNo(tid)));
				}
				break;

			case SOS_PROCESS_STATUS:
				syscall_reply(tid, process_write_status(
							(process_t*) pager_buffer(tid), L4_MsgWord(&msg, 0)));
				break;

			case SOS_PROCESS_DELETE:
				// dont try to reply to a thread we are deleting
				if (L4_ThreadNo(tid) == L4_MsgWord(&msg, 0)) {
					processDelete(L4_MsgWord(&msg, 0));
				} else {
					syscall_reply(tid, processDelete(L4_MsgWord(&msg, 0)));
				}
				break;

			case SOS_PROCESS_CREATE:
				pid = reserve_pid();

				if (pid != NIL_PID) {
					elfReq = allocElfload(
							pager_buffer(tid), process_get_pid(p), pid);
					queueRequest2(allocRequest(REQUEST_ELFLOAD, elfReq));
				} else {
					dprintf(0, "Out of processes\n");
					syscall_reply(tid, -1);
				}

				break;

			case SOS_DEBUG_FLUSH:
				pagerFlush();
				syscall_reply_v(tid, 0);
				break;

			case L4_EXCEPTION:
				dprintf(0, "Exception (ip=%p sp=%p id=0x%lx cause=0x%lx pid=%d)\n",
						(void*) L4_MsgWord(&msg, 0), (void*) L4_MsgWord(&msg, 1),
						L4_MsgWord(&msg, 2), L4_MsgWord(&msg, 3), L4_MsgWord(&msg, 4),
						process_get_pid(p));
				processDelete(process_get_pid(p));
				break;

			default:
				dprintf(0, "!!! pager: unhandled syscall tid=%ld id=%d name=%s\n",
						L4_ThreadNo(tid), TAG_SYSLAB(tag), syscall_show(TAG_SYSLAB(tag)));
		}

		dprintf(3, "*** pager_handler: finished %s from %d\n",
				syscall_show(TAG_SYSLAB(tag)), process_get_pid(p));
	}

	dprintf(0, "!!! pager_handler: loop failed!\n");
}

char *pager_buffer(L4_ThreadId_t tid) {
	return &copyInOutBuffer[L4_ThreadNo(tid) * MAX_IO_BUF];
}

// Like memcpy, but only copies up to the next page boundary.
// Returns 1 if reached the boundary, 0 otherwise.
static int memcpyPage(char *dst, char *src, size_t size) {
	int i = 0;

	while (i < size) {
		*dst = *src;

		dst++;
		src++;

		i++;

		if ((((L4_Word_t) dst) & ~PAGEALIGN) == 0) break;
		if ((((L4_Word_t) src) & ~PAGEALIGN) == 0) break;
	}

	return i;
}

static void copyInContinue2(Pagefault *fault) {
	dprintf(3, "*** %s: pid=%d addr=%p\n", __FUNCTION__,
			fault->pid, (void*) fault->addr);
	Process *p;
	L4_Word_t size, offset, addr;

	p = process_lookup(fault->pid);

	// Data about the copyin operation.
	size = min(MAX_IO_BUF, LO_HALF(copyInOutData[process_get_pid(p)]));
	offset = HI_HALF(copyInOutData[process_get_pid(p)]);
	addr = fault->addr + offset;

	// Prepare caches
	prepareDataIn(p, addr & PAGEALIGN);

	// Continue copying in from where we left off, and note that this function
	// will only get called from the pager so the page will be in memory
	L4_Word_t dst = (L4_Word_t) pager_buffer(process_get_tid(p)) + offset;
	L4_Word_t src = *pagetableLookup(process_get_pagetable(p), addr);

	src &= ADDRESS_MASK;
	src += fault->addr & ~PAGEALIGN; // offset in page

	dprintf(3, "*** %s: copying dst=%p src=%p size=%d offset=%d\n",
			__FUNCTION__, dst, src, size, offset);

	offset += memcpyPage((char*) dst, (char*) src, size - offset);
	assert(offset <= size);
	copyInOutData[process_get_pid(p)] = size | HI_SHIFT(offset);

	// Either we finished the copy or reached a page boundary
	if (offset == size) {
		L4_ThreadId_t tid = process_get_tid(p);
		free(fault);
		dprintf(3, "*** %s: finished\n", __FUNCTION__);
		syscall_reply_v(tid, 0);
	} else {
		assert(((fault->addr + offset) & ~PAGEALIGN) == 0);
		dprintf(2, "*** %s: continuing\n", __FUNCTION__);
		pager2(fault);
	}
}

static void copyIn(L4_ThreadId_t tid, void *src, size_t size, int append) {
	dprintf(3, "*** copyIn: tid=%ld src=%p size=%d\n",
			L4_ThreadNo(tid), src, size);
	Process *p;
	L4_Word_t newBase;
	size_t newSize;

	p = process_lookup(L4_ThreadNo(tid));

	// Prepare to copy in
	newSize = LO_HALF(copyInOutData[process_get_pid(p)]);
	newBase = HI_HALF(copyInOutData[process_get_pid(p)]);

	if (append) {
		newSize += size;
	} else {
		newSize = size;
		newBase = 0;
	}

	copyInOutData[process_get_pid(p)] = LO_HALF(newSize) | HI_SHIFT(newBase);

	// Force a page fault to start the copy in process
	pager2(allocPagefault(
				process_get_pid(p), (L4_Word_t) src, 0x4, copyInContinue2));
}

static void copyOutContinue2(Pagefault *fault) {
	dprintf(3, "*** %s: pid=%d addr=%p\n", __FUNCTION__,
			fault->pid, (void*) fault->addr);
	Process *p;
	L4_Word_t size, offset, addr;

	p = process_lookup(fault->pid);

	// Data about the copyin operation.
	size = min(MAX_IO_BUF, LO_HALF(copyInOutData[process_get_pid(p)]));
	offset = HI_HALF(copyInOutData[process_get_pid(p)]);
	addr = fault->addr + offset;

	// Continue copying in from where we left off, and note that this function
	// will only get called from the pager so the page will be in memory
	L4_Word_t src = (L4_Word_t) pager_buffer(process_get_tid(p)) + offset;
	L4_Word_t dst = *pagetableLookup(process_get_pagetable(p), addr);

	dst &= ADDRESS_MASK;
	dst += fault->addr & ~PAGEALIGN; // offset in page

	dprintf(3, "*** %s: copying dst=%p src=%p size=%d offset=%d\n",
			__FUNCTION__, dst, src, size, offset);

	offset += memcpyPage((char*) dst, (char*) src, size - offset);
	assert(offset <= size);
	copyInOutData[process_get_pid(p)] = size | HI_SHIFT(offset);

	// Prepare caches
	prepareDataOut(p, addr & PAGEALIGN);

	// Either we finished the copy or reached a page boundary
	if (offset == size) {
		L4_ThreadId_t tid = process_get_tid(p);
		free(fault);
		dprintf(3, "*** %s: finished\n", __FUNCTION__);
		syscall_reply_v(tid, 0);
	} else {
		assert(((fault->addr + offset) & ~PAGEALIGN) == 0);
		dprintf(2, "*** %s: continuing\n", __FUNCTION__);
		pager2(fault);
	}
}

static void copyOut(L4_ThreadId_t tid, void *dst, size_t size, int append) {
	dprintf(3, "*** copyOut: tid=%ld dst=%p size=%d\n",
			L4_ThreadNo(tid), dst, size);
	Process *p;
	L4_Word_t newBase;
	size_t newSize;

	p = process_lookup(L4_ThreadNo(tid));

	// Prepare to copy out
	newSize = LO_HALF(copyInOutData[process_get_pid(p)]);
	newBase = HI_HALF(copyInOutData[process_get_pid(p)]);

	if (append) {
		newSize += size;
	} else {
		newSize = size;
		newBase = 0;
	}

	copyInOutData[process_get_pid(p)] = LO_HALF(newSize) | HI_SHIFT(newBase);

	// Force a page fault to start the copy out process
	pager2(allocPagefault(
				process_get_pid(p), (L4_Word_t) dst, 0x2, copyOutContinue2));
}

