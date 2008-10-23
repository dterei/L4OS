#include <elf/elf.h>
#include <sos/sos.h>
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

#define verbose 1

// Masks for page table entries
#define SWAP_MASK (1 << 0)
#define REF_MASK  (1 << 1)
#define ELF_MASK  (1 << 2)
#define ADDRESS_MASK PAGEALIGN

// Ensure the kernel always has frames
#define KERNEL_FRAME_BUFFER 128
static int totalPages;

// Limiting the number of user frames
#define FRAME_ALLOC_LIMIT 400
static int allocLimit;

// Tracking allocated frames, including default swap file
static List *alloced; // [(pid, word)]
static List *swapped; // [(pid, word)]

#define PR_OFFSET_UNDEFINED (-1)
static Swapfile *defaultSwapfile;

// Asynchronous pager requests
typedef enum {
	REQUEST_SWAPOUT,
	REQUEST_SWAPIN,
	REQUEST_ELFLOAD,
} rtype_t;

static List *requests; // [(rtype_t, rdata)]

static void queueRequest(rtype_t rtype, void *request);
static void dequeueRequest(void);
static void startPagerRequest(void);
static void startRequest(void);
static void startSwapin(void);
static void startSwapout(void);

// Demand paging
#define SWAP_BUFSIZ 1024

typedef enum {
	SWAPIN_OPEN,
	SWAPIN_LSEEK,
	SWAPIN_READ,
	SWAPIN_CLOSE,
	SWAPOUT_OPEN,
	SWAPOUT_WRITE,
	SWAPOUT_CLOSE
} pr_stage_t;

typedef struct PagerRequest_t PagerRequest;
struct PagerRequest_t {
	pr_stage_t stage;
	pid_t pid;
	L4_Word_t addr;
	L4_Word_t diskAddr;
	Swapfile *sf;
	int rights;
	int offset;
	int size;
	void (*swapinFinish)(void);
	void (*callback)(PagerRequest *pr);
};

static L4_Word_t pinnedFrame;

// ELF loading
typedef enum {
	ELFLOAD_OPEN,
	ELFLOAD_CHECK_EXEC,
	ELFLOAD_READ_HEADER,
	ELFLOAD_CLOSE,
} elfload_stage_t;

typedef struct {
	elfload_stage_t stage;
	char path[MAX_FILE_NAME];  // path to executable
	fildes_t fd; // file descriptor (when opened)
	pid_t parent; // pid of the process that made the request
	pid_t child; // pid of the process created
} ElfloadRequest;

static L4_ThreadId_t virtualPager; // automatically L4_nilthread
static void virtualPagerHandler(void);

// For copyin/copyout
#define LO_HALF_MASK 0x0000ffff
#define LO_HALF(word) ((word) & 0x0000ffff)
#define HI_HALF_MASK 0xffff0000
#define HI_HALF(word) (((word) >> 16) & 0x0000ffff)
#define HI_SHIFT(word) ((word) << 16)

static L4_Word_t *copyInOutData;
static char *copyInOutBuffer;
static void copyIn(L4_ThreadId_t tid, void *src, size_t size, int append);
static void copyOut(L4_ThreadId_t tid, void *dst, size_t size, int append);

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
	Process *p = (Process*) data;  // (pid, word)

	if ((pid_t) curr->fst == process_get_pid(p)) {
		L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), curr->snd);
		pagerFrameFree(p, *entry & ADDRESS_MASK);
		free(curr);
		return 1;
	} else {
		return 0;
	}
}

static int isPageAligned(void *ptr) {
	return (((L4_Word_t) ptr) & (PAGESIZE - 1)) == 0;
}

static L4_Word_t *allocFrames(int n) {
	assert(n > 0);
	L4_Word_t frame, nextFrame;

	frame = frame_alloc(FA_ALLOCFRAMES);
	n--;

	for (int i = 1; i < n; i++) {
		nextFrame = frame_alloc(FA_ALLOCFRAMES);
		assert((frame + i * PAGESIZE) == nextFrame);
	}

	return (L4_Word_t*) frame;
}

static int mapPage(L4_SpaceId_t sid, L4_Word_t virt, L4_Word_t phys,
		int rights) {
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

static rtype_t requestsPeekType(void) {
	return (rtype_t) ((Pair*) list_peek(requests))->fst;
}

static void *requestsPeek(void) {
	return (void*) ((Pair*) list_peek(requests))->snd;
}

static void *requestsUnshift(void) {
	Pair *pair = (Pair*) list_unshift(requests);
	void *data = (void*) pair->snd;
	pair_free(pair);
	return data;
}

void pager_init(void) {
	// Set up lists
	totalPages = FRAME_ALLOC_LIMIT;
	//totalPages = frames_free();
	allocLimit = totalPages;
	alloced = list_empty();
	swapped = list_empty();
	requests = list_empty();

	// Grab a bunch of frames to use for copyin/copyout
	assert((PAGESIZE % MAX_IO_BUF) == 0);
	int numFrames = ((MAX_THREADS * MAX_IO_BUF) / PAGESIZE);

	copyInOutData = (L4_Word_t*) allocFrames(sizeof(L4_Word_t));
	copyInOutBuffer = (char*) allocFrames(numFrames);

	// The default swapfile (.swap)
	defaultSwapfile = swapfile_init(SWAPFILE_FN);

	// Start the real pager process
	Process *p = process_run_rootthread("virtual_pager", virtualPagerHandler, YES_TIMESTAMP);
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
	*base = region_get_base(heap) + region_get_size(heap);

	// Move the heap region so SOS knows about it.
	region_set_size(heap, nb + region_get_size(heap));

	// Have the option of returning 0 to signify no more memory.
	return 1;
}

static int memoryUsage(void) {
	return totalPages - allocLimit;
}

static int findRegion(void *contents, void *data) {
	Region *r = (Region*) contents;
	L4_Word_t addr = (L4_Word_t) data;

	return ((addr >= region_get_base(r)) &&
			(addr < region_get_base(r) + region_get_size(r)));
}

static int findPaRegion(void *contents, void *data) {
	// Like findRegion, but only at pagealigned granularity
	Region *r = (Region*) contents;
	L4_Word_t addr = (L4_Word_t) data;

	return ((addr >= (region_get_base(r) & PAGEALIGN)) &&
			(region_get_size(r), PAGESIZE));
}

static void panic(void) {
	assert(! "DON'T PANIC");
}

static PagerRequest *allocPagerRequest(pid_t pid, L4_Word_t addr, int rights,
		Swapfile *sf, void (*callback)(PagerRequest *pr)) {
	PagerRequest *newPr = (PagerRequest*) malloc(sizeof(PagerRequest));

	newPr->pid = pid;
	newPr->addr = addr;
	newPr->rights = rights;
	newPr->offset = PR_OFFSET_UNDEFINED;
	newPr->sf = sf;
	newPr->swapinFinish = panic;
	newPr->callback = callback;

	return newPr;
}

static ElfloadRequest *allocElfloadRequest(char *path, pid_t caller) {
	ElfloadRequest *er = (ElfloadRequest *) malloc(sizeof(ElfloadRequest));

	if (er != NULL) {
		er->stage = 0;
		strncpy(er->path, path, MAX_FILE_NAME);
		er->fd = VFS_NIL_FILE;
		er->parent = caller;
	}

	return er;
}

static void pagerContinue(PagerRequest *pr) {
	dprintf(3, "*** pagerContinue: replying to %d\n", pr->pid);

	L4_ThreadId_t replyTo = process_get_tid(process_lookup(pr->pid));
	free(pr);
	syscall_reply_v(replyTo, 0);
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
		pair_free(curr);
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
	pagetableFree(p);
	list_iterate(process_get_regions(p), regionsFree, NULL);
	list_destroy(process_get_regions(p));

	// Wake all waiting processes
	process_wake_all(process_get_pid(p));

	// And done
	free(p);

	return 0;
}

static int pagerAction(PagerRequest *pr) {
	Process *p;
	L4_Word_t frame, *entry;

	dprintf(2, "*** pagerAction: fault on ss=%d, addr=%p rights=%d\n",
			L4_SpaceNo(L4_SenderSpace()), pr->addr, pr->rights);

	p = process_lookup(pr->pid);
	if (p == NULL) {
		// Process died
		return 0;
	}

	// Find region it belongs in.
	dprintf(3, "*** pagerAction: finding region\n");
	Region *r = list_find(process_get_regions(p), findRegion, (void*) pr->addr);

	if (r == NULL) {
		printf("Segmentation fault (%d)\n", process_get_pid(p));
		processDelete(process_get_pid(p));
		return 0;
	}

	if ((region_get_rights(r) & pr->rights) == 0) {
		printf("Permission fault (%d)\n", process_get_pid(p));
		processDelete(process_get_pid(p));
		return 0;
	}

	// Place in, or retrieve from, page table.
	dprintf(3, "*** pagerAction: finding entry\n");
	entry = pagetableLookup(process_get_pagetable(p), pr->addr);
	frame = *entry & ADDRESS_MASK;
	dprintf(3, "*** pagerAction: entry %p found at %p\n", (void*) *entry, entry);

	if (*entry & SWAP_MASK) {
		// On disk, queue a swapin request
		dprintf(2, "*** pagerAction: page is on disk (%p)\n", (void*) *entry);
		queueRequest(REQUEST_SWAPIN, pr);
		return 0;
	} else if ((frame & ADDRESS_MASK) != 0) {
		// Already appears in page table as a frame, just got unmapped
		// (probably to update the refbit)
		dprintf(3, "*** pagerAction: got unmapped\n");
	} else if (region_map_directly(r)) {
		// Wants to be mapped directly (code/data probably).
		dprintf(3, "*** pagerAction: mapping directly\n");
		frame = pr->addr & PAGEALIGN;
	} else {
		// Didn't appear in frame table so we need to allocate a new one.
		// However there are potentially no free frames.
		dprintf(3, "*** pagerAction: allocating frame\n");

		frame = pagerFrameAlloc(p, pr->addr & PAGEALIGN);
		assert((frame & ~ADDRESS_MASK) == 0); // no flags set

		if (frame == 0) {
			dprintf(2, "*** pagerAction: no free frames\n");
			queueRequest(REQUEST_SWAPIN, pr);
			return 0;
		}
	}

	*entry = frame | REF_MASK;

	dprintf(3, "*** pagerAction: mapping vaddr=%p pid=%d frame=%p rights=%d\n",
			(void*) (pr->addr & PAGEALIGN), process_get_pid(p),
			(void*) frame, region_get_rights(r));
	mapPage(process_get_sid(p), pr->addr & PAGEALIGN, frame, region_get_rights(r));

	return 1;
}

static void pager(PagerRequest *pr) {
	if (pagerAction(pr)) {
		pr->callback(pr);
	} else {
		dprintf(3, "*** pager: pagerAction stalled\n");
	}
}

static void finishElfload(int rval) {
	assert(requestsPeekType() == REQUEST_ELFLOAD);
	ElfloadRequest *er = (ElfloadRequest *) requestsPeek();
	L4_ThreadId_t replyTo = process_get_tid(process_lookup(er->parent));

	free(er);
	syscall_reply(replyTo, rval);
	dequeueRequest();
}

static void setRegionOnElf(Process *p, Region *r, L4_Word_t addr) {
	assert((addr & ~PAGEALIGN) == 0);
	L4_Word_t *entry;
	L4_Word_t base = region_get_base(r);

	for (int size = 0; size < region_get_size(r); size += PAGESIZE) {
		entry = pagetableLookup(process_get_pagetable(p), base + size);
		*entry = (addr + size) | SWAP_MASK | ELF_MASK;
	}
}

static char *wordAlign(char *s) {
	unsigned int x = (unsigned int) s;
	x--;
	x += sizeof(L4_Word_t) - (x % sizeof(L4_Word_t));
	return (char*) x;
}

static void continueElfload(int vfsRval) {
	ElfloadRequest *er = (ElfloadRequest *) requestsPeek();
	struct Elf32_Header *header;
	char *buf;
	stat_t *elfStat;
	Process *p;

	switch (er->stage) {
		case ELFLOAD_OPEN:
			dprintf(2, "ELFLOAD_OPEN\n");
			if (vfsRval < 0) {
				dprintf(1, "*** continueElfload: failed to open\n");
				finishElfload(-1);
			} else {
				er->fd = vfsRval;
				copyInOutData[L4_ThreadNo(sos_my_tid())] = 0;
				strncpy(pager_buffer(sos_my_tid()), er->path, MAX_IO_BUF);
				statNonblocking();
			}

			er->stage++;
			break;

		case ELFLOAD_CHECK_EXEC:
			assert(vfsRval == 0);

			buf = pager_buffer(sos_my_tid());
			elfStat = (stat_t*) wordAlign(buf + strlen(buf) + 1);

			if (!(elfStat->st_fmode & FM_EXEC)) {
				dprintf(1, "*** continueElfload: not executable\n");
				finishElfload(-1);
			} else {
				readNonblocking(er->fd, MAX_IO_BUF);
				er->stage++;
			}

			break;

		case ELFLOAD_READ_HEADER:
			dprintf(2, "ELFLOAD_READ_HEADER\n");
			header = (struct Elf32_Header*) pager_buffer(sos_my_tid());

			if (elf32_checkFile(header) != 0) {
				dprintf(1, "*** continueElfload: not an ELF file\n");
				finishElfload(-1);
			} else {
				p = process_init(PS_TYPE_PROCESS);

				for (int i = 0; i < elf32_getNumProgramHeaders(header); i++) {
					Region *r = region_alloc(
							REGION_OTHER,
							elf32_getProgramHeaderVaddr(header, i),
							elf32_getProgramHeaderMemorySize(header, i),
							elf32_getProgramHeaderFlags(header, i), 0);
					region_set_elffile(r, swapfile_init(er->path));
					region_set_filesize(r, elf32_getProgramHeaderFileSize(
								header, i));
					process_add_region(p, r);
					setRegionOnElf(p, r,
							elf32_getProgramHeaderOffset(header, i) & PAGEALIGN);
				}

				process_set_name(p, er->path);
				process_get_info(p)->pid = er->child;
				process_prepare(p);
				process_set_ip(p, (void*) elf32_getEntryPoint(header));

				process_run(p, YES_TIMESTAMP);
				closeNonblocking(er->fd);
				er->child = process_get_pid(p);
			}

			er->stage++;
			break;

		case ELFLOAD_CLOSE:
			dprintf(2, "ELFLOAD_CLOSE\n");
			finishElfload(er->child);
			break;

		default:
			assert(!"continueElfload");
	}
}

static void startElfload(void) {
	assert(requestsPeekType() == REQUEST_ELFLOAD);

	// Open the file and let the continuation take over
	ElfloadRequest *er = (ElfloadRequest *) requestsPeek();
	strncpy(pager_buffer(sos_my_tid()), er->path, MAX_IO_BUF);
	openNonblocking(NULL, FM_READ);
}

static void printRequests(void *contents, void *data) {
	Pair *pair = (Pair*) contents;
	rtype_t type = (rtype_t) pair->fst;
	PagerRequest *pr;
	ElfloadRequest *er;

	printf("rtype: %d, ", type);

	switch (type) {
		case REQUEST_SWAPOUT:
		case REQUEST_SWAPIN:
			pr = (PagerRequest*) pair->snd;
			printf("stage=%d pid=%d addr=%p\n",
					pr->stage, pr->pid, (void*) pr->addr);
			break;

		case REQUEST_ELFLOAD:
			er = (ElfloadRequest *) pair->snd;
			printf("stage=%d path=%s parent=%d child=%d\n",
					er->stage, er->path, er->parent, er->child);
			break;

		default:
			assert(!"default");
	}
}

static void dequeueRequest(void) {
	dprintf(1, "*** dequeueRequest\n");

	pair_free(list_unshift(requests));

	if (list_null(requests)) {
		dprintf(1, "*** dequeueRequest: no more items\n");
	} else {
		dprintf(1, "*** dequeueRequest: running next\n");
		if (verbose > 2) list_iterate(requests, printRequests, NULL);
		startRequest();
	}
}

static void queueRequest(rtype_t rtype, void *request) {
	dprintf(1, "*** queueRequest\n");

	if (list_null(requests)) {
		dprintf(2, "*** queueRequest: list null, running request\n");
		list_push(requests, pair_alloc(rtype, (L4_Word_t) request));
		startRequest();
	} else {
		dprintf(2, "*** queueRequest: queueing request\n");
		if (verbose > 2) list_iterate(requests, printRequests, NULL);
		list_push(requests, pair_alloc(rtype, (L4_Word_t) request));
	}
}

static void finishSwapout(void) {
	dprintf(1, "*** finishSwapout\n");

	Process *p;
	PagerRequest *swapin, *swapout;
	L4_Word_t *entry, frame;

	// Done with the swapout
	assert(requestsPeekType() == REQUEST_SWAPOUT);
	swapout = (PagerRequest*) requestsUnshift();

	pagerFrameFree(process_lookup(swapout->pid), swapout->addr);
	free(swapout);

	// Either the swapout was just for a free frame, or it was for
	// a frame with existing contents
	assert(requestsPeekType() == REQUEST_SWAPIN);
	swapin = (PagerRequest*) requestsPeek();

	p = process_lookup(swapin->pid);
	if (p == NULL) {
		// Process died
		swapin->callback(swapin);
		dequeueRequest();
	}

	// Pager is now guaranteed to find a page (note that the pager
	// action has been separated, we reply later)
	pagerAction(swapin);

	entry = pagetableLookup(process_get_pagetable(p), swapin->addr);
	frame = *entry & ADDRESS_MASK;

	if (pinnedFrame != 0) {
		// There is contents we need to copy across
		dprintf(2, "*** finishSwapout: pinned frame is %p\n", pinnedFrame);
		memcpy((char*) frame, (void*) pinnedFrame, PAGESIZE);
		frame_free(pinnedFrame);
		pinnedFrame = 0;
	} else {
		// Zero the frame for debugging, but it may be a good idea anyway
		dprintf(2, "*** zeroing frame %p\n", (void*) frame);
		memset((char*) frame, 0x00, PAGESIZE);
	}

	dprintf(2, "*** finishSwapout: addr=%p for pid=%d now %p\n",
			(void*) swapin->addr, process_get_pid(p), (void*) frame);

	prepareDataOut(p, swapin->addr & PAGEALIGN);
	swapin->callback(swapin);
	dequeueRequest();
}

static void finishSwapin(void) {
	dprintf(1, "*** finishSwapin\n");
	Process *p;
	PagerRequest *request;
	Pair args; // (pid, word)
	L4_Word_t *entry, addr;

	assert(requestsPeekType() == REQUEST_SWAPIN);
	request = (PagerRequest*) requestsPeek();
	p = process_lookup(request->pid);

	if (p == NULL) {
		// Process died
		frame_free(pinnedFrame);
		pinnedFrame = 0;
		request->callback(request);
		dequeueRequest();
	}

	// Either there is a frame free which we can immediately copy
	// the fresh page in to, or there isn't in which case we need
	// to swap something out first
	addr = request->addr & PAGEALIGN;
	entry = pagetableLookup(process_get_pagetable(p), addr);

	// In either case the page is no longer on disk
	assert(*entry & SWAP_MASK);

	args = PAIR(process_get_pid(p), *entry & ADDRESS_MASK);
	list_delete(swapped, pagerSwapslotFree, &args);
	*entry &= ~SWAP_MASK;
	*entry &= ~ADDRESS_MASK;

	if (allocLimit > 0) {
		// Pager is guaranteed to find a page
		pagerAction(request);
		L4_Word_t frame = *entry & ADDRESS_MASK;

		// Copy data from pinned frame to newly allocated frame
		memcpy((void*) frame, (void*) pinnedFrame, PAGESIZE);

		// Fix caches
		prepareDataOut(p, addr);

		frame_free(pinnedFrame);
		pinnedFrame = 0;
		request->callback(request);
		dequeueRequest();
	} else {
		// Need to swap something out before being able to continue -
		// the swapout process will deal with the pinned frame etc
		startSwapout();
	}
}

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
		memset((char*) pinnedFrame + (fileTop % PAGESIZE), 0x00,
				region_get_size(r) - region_get_filesize(r));
	}

	// Page no longer on ELF file.  Yay.
	*entry &= ~ELF_MASK;

	// I'm 99.99% sure it's ok to do this without any other magic
	finishSwapin();
}

static void continueSwapin(int vfsRval) {
	PagerRequest *pr = (PagerRequest*) requestsPeek();

	switch (pr->stage) {
		case SWAPIN_OPEN: 
			swapfile_set_fd(pr->sf, vfsRval);

			L4_Word_t *entry = pagetableLookup(
					process_get_pagetable(process_lookup(pr->pid)),
					pr->addr & ADDRESS_MASK);

			lseekNonblocking(swapfile_get_fd(pr->sf),
					*entry & ADDRESS_MASK, SEEK_SET);
			pr->offset = 0;

			pr->stage++;
			break;

		case SWAPIN_LSEEK:
			readNonblocking(swapfile_get_fd(pr->sf),
					min(SWAP_BUFSIZ, pr->size - pr->offset));
			pr->stage++;
			break;

		case SWAPIN_READ:
			assert(vfsRval > 0);
			assert(pr->offset <= PAGESIZE);

			memcpy(((char*) pinnedFrame) + pr->offset,
					pager_buffer(virtualPager), vfsRval);
			pr->offset += vfsRval;
			assert(pr->offset <= pr->size);

			if (pr->offset == pr->size) {
				swapfile_close(pr->sf);
				pr->stage++;
			} else {
				readNonblocking(swapfile_get_fd(pr->sf),
						min(SWAP_BUFSIZ, pr->size - pr->offset));
			}

			break;

		case SWAPIN_CLOSE:
			pr->swapinFinish();
			break;

		default:
			assert(!"default");
	}
}

static void continueSwapout(int vfsRval) {
	PagerRequest *pr = (PagerRequest*) requestsPeek();
	assert(pr->offset <= PAGESIZE);

	switch (pr->stage) {
		case SWAPOUT_OPEN:
			swapfile_set_fd(pr->sf, vfsRval);
			pr->offset = 0;

			lseekNonblocking(swapfile_get_fd(pr->sf), pr->diskAddr, SEEK_SET);

			pr->stage++;
			break;

		case SWAPOUT_WRITE:
			assert(pr->offset <= pr->size);

			if (pr->offset == pr->size) {
				swapfile_close(pr->sf);
				pr->stage++;
			} else {
				int size = min(SWAP_BUFSIZ, pr->size);
				memcpy(pager_buffer(sos_my_tid()),
						(void*) (pr->addr + pr->offset), size);
				writeNonblocking(swapfile_get_fd(pr->sf), size);
				pr->offset += size;
			}

			break;

		case SWAPOUT_CLOSE:
			finishSwapout();
			break;

		default:
			assert(!"default");
	}
}

static void startSwapin(void) {
	dprintf(2, "*** startSwapin\n");
	Process *p;
	PagerRequest *pr;
	Region *r;
	L4_Word_t *entry;

	// pin a frame to copy in to (although "pinned" is somewhat of a misnomer
	// since really it's just a temporary frame and nothing is really pinned)
	pinnedFrame = frame_alloc(FA_SWAPPIN);

	// kick-start the chain of NFS requests and callbacks by opening swapfile
	assert(requestsPeekType() == REQUEST_SWAPIN);
	pr = requestsPeek();
	p = process_lookup(pr->pid);
	entry = pagetableLookup(process_get_pagetable(p), pr->addr);

	r = list_find(process_get_regions(p), findRegion, (void*) pr->addr);
	assert(r != NULL);

	if (*entry & ELF_MASK) {
		// It is on an ELF file, need to read from that
		assert(pr->sf != NULL);
		pr->sf = region_get_elffile(r);
		pr->swapinFinish = finishSwapelf;
	} else {
		// It is the default swapfile
		pr->sf = defaultSwapfile;
		pr->swapinFinish = finishSwapin;
	}

	pr->size = PAGESIZE;
	pr->stage = SWAPIN_OPEN;
	swapfile_open(pr->sf, FM_READ);
}

/*
static void printRegion(void *contents, void *data) {
	Region *r = (Region*) contents;

	printf("*** region %p -> %p (%p) rights=%d\n",
			(void*) region_get_base(r),
			(void*) (region_get_base(r) + region_get_size(r)),
			(void*) r, region_get_rights(r));
}
*/

static void startSwapout(void) {
	dprintf(2, "*** startSwapout\n");

	Process *p;
	PagerRequest *swapoutPr;
	Region *r;
	Swapfile *sf;
	Pair *swapout; // (pid, word)
	L4_Word_t *entry, frame;

	// Choose the next page to swap out
	swapout = deleteAllocList();
	p = process_lookup(swapout->fst);
	assert(p != NULL);

	entry = pagetableLookup(process_get_pagetable(p), swapout->snd);
	frame = *entry & ADDRESS_MASK;

	// Need the region for finding the swap file
	r = list_find(process_get_regions(p), findPaRegion, (void*) swapout->snd);
	//list_iterate(process_get_regions(p), printRegion, NULL);
	//printf(" --- %p\n", (void*) swapout->snd);
	assert(r != NULL);
	sf = defaultSwapfile;

	// Make sure the frame reflects what is stored in the frame
	assert((swapout->snd & ~PAGEALIGN) == 0);
	prepareDataIn(p, swapout->snd);

	// The page is no longer backed
	unmapPage(process_get_sid(p), swapout->snd);

	dprintf(1, "*** startSwapout: addr=%p for pid=%d was %p\n",
			(void*) swapout->snd, process_get_pid(p), (void*) frame);

	// Set up the swapout
	swapoutPr = allocPagerRequest(swapout->fst, frame, FM_READ, sf, NULL);
	list_shift(requests, pair_alloc(REQUEST_SWAPOUT, (L4_Word_t) swapoutPr));

	// Set up where on disk to put the page
	L4_Word_t diskAddr = pagerSwapslotAlloc(p);
	assert((diskAddr & ~PAGEALIGN) == 0);
	dprintf(1, "*** startSwapout: swapslot is 0x%08lx\n", diskAddr);

	*entry |= SWAP_MASK;
	*entry &= ~ADDRESS_MASK;
	*entry |= diskAddr;

	// We open the swapfile on each swapout
	swapoutPr->diskAddr = diskAddr;
	swapoutPr->stage = SWAPOUT_OPEN;
	swapoutPr->size = PAGESIZE;
	swapfile_open(sf, FM_WRITE);
	pair_free(swapout);
}

static void startRequest(void) {
	dprintf(1, "*** startRequest\n");
	assert(!list_null(requests));

	switch (requestsPeekType()) {
		case REQUEST_SWAPOUT:
		case REQUEST_SWAPIN:
			startPagerRequest();
			break;

		case REQUEST_ELFLOAD:
			startElfload();
			break;

		default:
			dprintf(0, "!!! startRequest: unrecognised request\n");
	}
}

static void startPagerRequest(void) {
	dprintf(1, "*** startPagerRequest\n");
	assert(!list_null(requests));

	// This is called to start running the next pager request, assuming
	// there is another one 
	assert(requestsPeekType() != REQUEST_ELFLOAD);
	PagerRequest *nextPr = requestsPeek();

	L4_Word_t *entry = pagetableLookup(
			process_get_pagetable(process_lookup(nextPr->pid)), nextPr->addr);

	if (*entry & SWAP_MASK) {
		// At the very least a page needs to be swapped in first
		startSwapin();
		// Afterwards, may have to swap something out
	} else {
		// Needed to swap something out
		assert(pinnedFrame == 0);

		if (allocLimit > 0) {
			// In the meantime a frame has become free
			pager((PagerRequest*) requestsUnshift());
		} else {
			startSwapout();
		}
	}
}

static void vfsHandler(int vfsRval) {
	dprintf(2, "*** vfsHandler: vfsRval=%d\n", vfsRval);

	switch (requestsPeekType()) {
		case REQUEST_SWAPOUT:
			dprintf(3, "*** demandPager: swapout continuation\n");
			continueSwapout(vfsRval);
			break;

		case REQUEST_SWAPIN:
			dprintf(3, "*** demandPager: swapin continuation\n");
			continueSwapin(vfsRval);
			break;

		case REQUEST_ELFLOAD:
			dprintf(3, "*** vfsHandler: elfload continuation\n");
			continueElfload(vfsRval);
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

static void virtualPagerHandler(void) {
	L4_Accept(L4_AddAcceptor(L4_UntypedWordsAcceptor, L4_NotifyMsgAcceptor));

	virtualPager = sos_my_tid();
	dprintf(1, "*** virtualPagerHandler: tid=%ld\n", L4_ThreadNo(virtualPager));

	L4_Msg_t msg;
	L4_MsgTag_t tag;
	L4_ThreadId_t tid = L4_nilthread;
	Process *p = NULL;;
	ElfloadRequest *er = NULL;
	pid_t pid = NIL_PID;
	L4_Word_t tmp;

	for (;;) {
		tag = L4_Wait(&tid);

		tid = sos_sid2tid(L4_SenderSpace());
		p = process_lookup(L4_ThreadNo(tid));
		L4_MsgStore(tag, &msg);

		if (!L4_IsSpaceEqual(L4_SenderSpace(), L4_rootspace)) {
			dprintf(2, "*** virtualPagerHandler: tid=%ld tag=%s\n",
					L4_ThreadNo(tid), syscall_show(TAG_SYSLAB(tag)));
		}

		switch (TAG_SYSLAB(tag)) {
			case L4_PAGEFAULT:
				pager(allocPagerRequest(process_get_pid(p), L4_MsgWord(&msg, 0),
							L4_Label(tag) & 0x7, defaultSwapfile, pagerContinue));
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
				if (L4_IsSpaceEqual(L4_SenderSpace(), L4_rootspace)) {
					vfsHandler(L4_MsgWord(&msg, 0));
				} else {
					dprintf(0, "!!! virtualPagerHandler: got reply from user\n");
				}
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
					er = allocElfloadRequest(pager_buffer(tid), process_get_pid(p));
				}

				if (er == NULL || pid == NIL_PID) {
					dprintf(0, "Out of processes!\n");
					syscall_reply(tid, -1);
					if (er != NULL) {
						free(er);
					}
				} else {
					er->child = pid;
					queueRequest(REQUEST_ELFLOAD, er);
				}
				er = NULL;
				pid = NIL_PID;
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

		dprintf(2, "*** virtualPagerHandler: finished %s from %d\n",
				syscall_show(TAG_SYSLAB(tag)), process_get_pid(p));
	}

	dprintf(0, "!!! virtualPagerHandler: loop failed!\n");
}

char *pager_buffer(L4_ThreadId_t tid) {
	return &copyInOutBuffer[L4_ThreadNo(tid) * MAX_IO_BUF];
}

static void copyInContinue(PagerRequest *pr) {
	dprintf(3, "*** copyInContinue pr=%p pid=%d addr=%p\n",
			pr, pr->pid, (void*) pr->addr);

	Process *p = process_lookup(pr->pid);
	L4_Word_t addr = pr->addr & PAGEALIGN;

	// Prepare caches
	prepareDataIn(p, addr);

	// Data about the copyin operation.
	L4_Word_t size = min(MAX_IO_BUF, LO_HALF(copyInOutData[process_get_pid(p)]));
	L4_Word_t offset = HI_HALF(copyInOutData[process_get_pid(p)]);

	// Continue copying in from where we left off
	char *dst = pager_buffer(process_get_tid(p)) + offset;

	// Assume it's already there - this function should only get
	// called as a continutation from the pager, so no problem
	char *src = (char*) (*pagetableLookup(process_get_pagetable(p), addr)
			& ADDRESS_MASK);
	src += pr->addr & ~PAGEALIGN;

	// Start the copy - if we reach a page boundary then pause computation
	// since we can assume that it goes out to disk and waits a long time
	while (offset < size) {
		*dst = *src;

		dst++;
		src++;
		offset++;

		if (isPageAligned(src)) break; // continue from next page boundary
	}

	copyInOutData[process_get_pid(p)] = size | (offset << 16);

	// Reached end - either we finished the copy, or we reached a boundary
	if (offset == size) {
		L4_ThreadId_t tid = process_get_tid(p);
		free(pr);
		dprintf(3, "*** copyInContinue: finished\n");
		syscall_reply_v(tid, 0);
	} else {
		pr->addr = (L4_Word_t) src;
		dprintf(3, "*** copyInContinue: continuing\n");
		pager(pr);
	}
}

static void copyIn(L4_ThreadId_t tid, void *src, size_t size, int append) {
	dprintf(3, "*** copyIn: tid=%ld src=%p size=%d\n",
			L4_ThreadNo(tid), src, size);
	Process *p = process_lookup(L4_ThreadNo(tid));

	// Prepare to copy in
	size_t newSize = LO_HALF(copyInOutData[process_get_pid(p)]);
	L4_Word_t newBase = HI_HALF(copyInOutData[process_get_pid(p)]);

	if (append) {
		newSize += size;
	} else {
		newSize = size;
		newBase = 0;
	}

	copyInOutData[process_get_pid(p)] = LO_HALF(newSize) | HI_SHIFT(newBase);

	// Force a page fault to start the process
	pager(allocPagerRequest( process_get_pid(p), (L4_Word_t) src,
				FM_READ, defaultSwapfile, copyInContinue));
}

static void copyOutContinue(PagerRequest *pr) {
	dprintf(3, "*** copyOutContinue pr=%p pid=%ld addr=%p\n",
			pr, pr->pid, (void*) pr->addr);

	Process *p = process_lookup(pr->pid);
	L4_Word_t addr = pr->addr & PAGEALIGN;

	// Data about the copyout operation.
	L4_Word_t size = min(MAX_IO_BUF, LO_HALF(copyInOutData[process_get_pid(p)]));
	L4_Word_t offset = HI_HALF(copyInOutData[process_get_pid(p)]);

	// Continue copying out from where we left off
	char *src = pager_buffer(process_get_tid(p)) + offset;
	char *dst = (char*) (*pagetableLookup(process_get_pagetable(p), addr)
				& ADDRESS_MASK);
	dst += pr->addr & ~PAGEALIGN;

	// Start the copy, same story as copyin
	while (offset < size) {
		*dst = *src;

		dst++;
		src++;
		offset++;

		if (isPageAligned(dst)) break; // continue from next page boundary
	}

	// Remember data
	copyInOutData[process_get_pid(p)] = size | HI_SHIFT(offset);

	// Fix caches
	prepareDataOut(p, addr);

	if (offset == size) {
		L4_ThreadId_t tid = process_get_tid(p);
		free(pr);
		dprintf(3, "*** copyOutContinue: finished\n");
		syscall_reply_v(tid, 0);
	} else {
		pr->addr = (L4_Word_t) dst;
		dprintf(3, "*** copyOutContinue: continuing\n");
		pager(pr);
	}
}

static void copyOut(L4_ThreadId_t tid, void *dst, size_t size, int append) {
	dprintf(3, "*** copyOut: tid=%ld dst=%p size=%d\n",
			L4_ThreadNo(tid), dst, size);
	Process *p = process_lookup(L4_ThreadNo(tid));

	// Prepare to copy out
	size_t newSize = LO_HALF(copyInOutData[process_get_pid(p)]);
	L4_Word_t newBase = HI_HALF(copyInOutData[process_get_pid(p)]);

	if (append) {
		newSize += size;
	} else {
		newSize = size;
		newBase = 0;
	}

	copyInOutData[process_get_pid(p)] = LO_HALF(newSize) | HI_SHIFT(newBase);

	// Force a page fault to start the process
	pager(allocPagerRequest(process_get_pid(p), (L4_Word_t) dst,
				FM_WRITE, defaultSwapfile, copyOutContinue));
}

