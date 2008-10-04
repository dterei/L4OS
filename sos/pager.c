#include <sos/sos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "frames.h"
#include "l4.h"
#include "libsos.h"
#include "pager.h"
#include "process.h"
#include "swapfile.h"
#include "syscall.h"

#define verbose 2

// For (demand and otherwise) paging
#define FRAME_ALLOC_LIMIT 4 // limited by the swapfile size

#define ONDISK_MASK  0x00000001
#define PINNED_MASK  0x00000002
#define REFBIT_MASK  0x00000004
#define ADDRESS_MASK 0xfffff000

typedef struct FrameList_t FrameList;
struct FrameList_t {
	Process   *p;     // process currently allocated to
	L4_Word_t  page;  // virtual page within that process
	FrameList *next;
};

static int allocLimit;
static FrameList *allocHead;
static FrameList *allocLast;

// Asynchronous pager requests
#define SWAP_BUFSIZ 256

struct PagerRequest_t {
	Process *p;
	L4_Word_t addr;
	int offset;
	void (*callback)(PagerRequest *pr);
	PagerRequest *next;
};

static PagerRequest *requestsHead = NULL;
static PagerRequest *requestsLast = NULL;
static PagerRequest swapoutRequest;
static L4_Word_t pinnedFrame;

static void queueRequest(PagerRequest *pr);

// For the pager process
#define PAGER_STACK_SIZE PAGESIZE

static L4_Word_t virtualPagerStack[PAGER_STACK_SIZE];
L4_ThreadId_t virtual_pager; // automatically initialised to 0 (L4_nilthread)
static void virtualPagerHandler(void);

// For copyin/copyout
#define LO_HALF_MASK 0x0000ffff
#define LO_HALF(word) ((word) & 0x0000ffff)
#define HI_HALF_MASK 0xffff0000
#define HI_HALF(word) (((word) >> 16) & 0x0000ffff)

static L4_Word_t *copyInOutData;
static char *copyInOutBuffer;
static void copyIn(L4_ThreadId_t tid, void *src, size_t size, int append);
static void copyOut(L4_ThreadId_t tid, void *dest, size_t size, int append);

// Page table structures
// Each level of the page table is assumed to be of size PAGESIZE
typedef struct Pagetable2_t {
	L4_Word_t pages[PAGEWORDS];
} Pagetable2;

typedef struct Pagetable1_t {
	Pagetable2 *pages2[PAGEWORDS];
} Pagetable1;

// Region structure
struct Region_t {
	region_type type; // type of region (heap? stack?)
	uintptr_t base;   // base of the region
	uintptr_t size;   // size of region
	int rights;       // access rights of region (read, write, execute)
	int mapDirectly;  // do we directly (1:1) map it?
	int id;           // what bootinfo uses to identify the region
	struct Region_t *next;
};

static void pagerReply(L4_ThreadId_t tid) {
	L4_Msg_t msg;
	L4_MsgClear(&msg);
	L4_Set_MsgLabel(&msg, SOS_REPLY << 4);
	L4_MsgLoad(&msg);

	if (L4_IpcFailed(L4_Reply(tid))) {
		dprintf(0, "!!! pagerReply to %ld failed: ", L4_ThreadNo(tid));
		sos_print_error(L4_ErrorCode());
	}
}

static void memdump(char *addr, int size) {
	(void) memdump;
	int perRow = 32;

	for (int row = 0; row < size; row += perRow) {
		printf("%04x:", row);
		for (int col = 0; col < perRow; col += 2) {
			printf(" %02x%02x", addr[row + col], addr[row + col + 1]);
		}
		printf("\n");
	}
}

Region *region_init(region_type type, uintptr_t base,
		uintptr_t size, int rights, int dirmap) {
	Region *new = (Region*) malloc(sizeof(Region));

	new->type = type;
	new->base = base;
	new->size = size;
	new->rights = rights;
	new->mapDirectly = dirmap;

	return new;
}

uintptr_t region_base(Region *r) {
	return r->base;
}

uintptr_t region_size(Region *r) {
	return r->size;
}

Region *region_next(Region *r) {
	return r->next;
}

void region_set_rights(Region *r, int rights) {
	r->rights = rights;
}

void region_append(Region *r, Region *toAppend) {
	r->next = toAppend;
}

void region_free_all(Region *r) {
	while (r != NULL) {
		Region *next = r->next;
		free(r);
		r = next;
	}
}

Pagetable *pagetable_init(void) {
	assert(sizeof(Pagetable1) == PAGESIZE);
	Pagetable1 *pt = (Pagetable1*) frame_alloc();

	for (int i = 0; i < PAGEWORDS; i++) {
		pt->pages2[i] = NULL;
	}

	return (Pagetable*) pt;
}

static L4_Word_t* pagetableLookup(Pagetable *pt, L4_Word_t addr) {
	assert((addr & ~PAGEALIGN) == 0);
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
		level1->pages2[offset1] = (Pagetable2*) frame_alloc();

		for (int i = 0; i < PAGEWORDS; i++) {
			level1->pages2[offset1]->pages[i] = 0;
		}
	}

	return &level1->pages2[offset1]->pages[offset2];
}

static void pagerFrameFree(L4_Word_t frame) {
	assert((frame & ~PAGEALIGN) == 0);
	frame_free(frame);
	allocLimit++;
}

void pagetable_free(Pagetable *pt) {
	Pagetable1 *pt1 = (Pagetable1*) pt;

	for (int i = 0; i < PAGEWORDS; i++) {
		if (pt1->pages2[i] != NULL) {
			frame_free((L4_Word_t) pt1->pages2[i]);
		}
	}

	frame_free((L4_Word_t) pt1);
}

void frames_free(pid_t pid) {
	(void) pagerFrameFree;

	/*
	for (FrameList *list = allocHead; list != NULL; list = list->next) {
		if (list->p != NULL && process_get_pid(list->p) == pid) {
			pagerFrameFree(list->frame);
		}
	}
	*/
}

static int isPageAligned(void *ptr) {
	return (((L4_Word_t) ptr) & (PAGESIZE - 1)) == 0;
}

static L4_Word_t *allocFrames(int n) {
	assert(n > 0);
	L4_Word_t frame, nextFrame;

	frame = frame_alloc();
	n--;

	for (int i = 1; i < n; i++) {
		nextFrame = frame_alloc();
		assert((frame + i * PAGESIZE) == nextFrame);
	}

	return (L4_Word_t*) frame;
}

static int mapPage(L4_SpaceId_t sid, L4_Word_t virt, L4_Word_t phys,
		int rights) {
	L4_Fpage_t fpage = L4_Fpage(virt, PAGESIZE);
	L4_Set_Rights(&fpage, rights);
	L4_PhysDesc_t ppage = L4_PhysDesc(phys, L4_DefaultMemory);

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
	// Prepare for some data from a user program to be fiddled with
	// by the pager.  This involves flushing the user programs cache on
	// this address, and invalidating our own cache.
	vaddr &= PAGEALIGN;
	L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), vaddr);
	L4_Word_t frame = *entry & ADDRESS_MASK;

	dprintf(2, "*** prepareDataIn: p=%d vaddr=%p frame=%p\n",
			process_get_pid(p), (void*) vaddr, (void*) frame);

	please(L4_CacheFlushRange(process_get_sid(p), vaddr, vaddr + PAGESIZE));
	please(L4_CacheFlushRangeInvalidate(L4_rootspace, frame, frame + PAGESIZE));
}

static void prepareDataOut(Process *p, L4_Word_t vaddr) {
	// Prepare from some data that has been changed in here to be reflected
	// on the user space.  This involves flushing our own cache, then
	// invalidating the user's cache on the address.
	vaddr &= PAGEALIGN;
	L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), vaddr);
	L4_Word_t frame = *entry & ADDRESS_MASK;

	dprintf(2, "*** prepareDataOut: p=%d vaddr=%p frame=%p\n",
			process_get_pid(p), (void*) vaddr, (void*) frame);

	please(L4_CacheFlushRange(L4_rootspace, frame, frame + PAGESIZE));
	please(L4_CacheFlushRangeInvalidate(
				process_get_sid(p), vaddr, vaddr + PAGESIZE));
}

static void addFrameList(Process *p, L4_Word_t page) {
	FrameList *new = (FrameList*) malloc(sizeof(FrameList));

	new->p = p;
	new->page = page;

	if (allocHead == NULL) {
		assert(allocLast == NULL);
		allocLast = new;
		allocHead = new;
	} else {
		new->next = NULL;
		allocLast->next = new;
		allocLast = new;
	}
}

static FrameList *deleteFrameList(void) {
	assert(allocHead != NULL);
	assert(allocLast != NULL);

	FrameList *tmp;
	FrameList *found = NULL;

	// Second-chance algorithm
	while (found == NULL) {
		L4_Word_t *vpage = pagetableLookup(
				process_get_pagetable(allocHead->p), allocHead->page);

		dprintf(3, "*** deleteFrameList: p=%d page=%p frame=%p\n",
				process_get_pid(allocHead->p), (void*) allocHead->page,
				(void*) (*vpage & ADDRESS_MASK));

		if ((*vpage & REFBIT_MASK) == 0) {
			// Not been referenced, this is the frame to swap
			found = allocHead;
			allocHead = allocHead->next;
			if (allocHead == NULL) allocLast = NULL;
		} else {
			// Been referenced, clear it
			*vpage &= ~REFBIT_MASK;

			// unmap (so if it's referenced again the pager will get
			// called and the refbit will be reset)
			// (un)comment to expose a different bug
			unmapPage(process_get_sid(allocHead->p), allocHead->page);

			// and move to back
			if (allocHead->next != NULL) {
				assert(allocHead != allocLast);
				tmp = allocHead;
				allocHead = allocHead->next;
				allocLast->next = tmp;
				tmp->next = NULL;
				allocLast = tmp;
			}
		}
	}

	return found;
}

static L4_Word_t pagerFrameAlloc(Process *p, L4_Word_t vaddr) {
	L4_Word_t frame;

	assert(allocLimit >= 0);

	if (allocLimit == 0) {
		dprintf(1, "*** pagerFrameAlloc: allocLimit reached\n");
		frame = 0;
	} else {
		frame = frame_alloc();
		dprintf(1, "*** pagerFrameAlloc: allocated frame %p\n", frame);

		process_get_info(p)->size++;
		allocLimit--;

		L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), vaddr);
		*entry &= ~ADDRESS_MASK;
		*entry |= frame;
		// TODO cache magic?

		addFrameList(p, vaddr);
	}

	return frame;
}

void pager_init(void) {
	// Set up alloc frame list
	allocLimit = FRAME_ALLOC_LIMIT;
	allocHead = NULL;
	allocLast = NULL;

	// Grab a bunch of frames to use for copyin/copyout
	assert((PAGESIZE % MAX_IO_BUF) == 0);
	int numFrames = ((MAX_THREADS * MAX_IO_BUF) / PAGESIZE);

	dprintf(1, "*** pager_init: grabbing %d frames\n", numFrames);

	copyInOutData = (L4_Word_t*) allocFrames(sizeof(L4_Word_t));
	copyInOutBuffer = (char*) allocFrames(numFrames);

	// Start the real pager process
	Process *pager = process_init();

	process_prepare(pager);
	process_set_ip(pager, (void*) virtualPagerHandler);
	process_set_sp(pager, virtualPagerStack + PAGER_STACK_SIZE - 1);

	// Start pager, but wait until it has actually started before
	// trying to assign virtual_pager to anything
	dprintf(1, "*** pager_init: about to run pager\n");
	process_run(pager, RUN_AS_THREAD);
}

int sos_moremem(uintptr_t *base, unsigned int nb) {
	dprintf(2, "*** sos_moremem(%p, %lx)\n", base, nb);

	// Find the current heap section.
	Process *p = process_lookup(L4_SpaceNo(L4_SenderSpace()));
	Region *heap;

	for (heap = process_get_regions(p); heap != NULL; heap = heap->next) {
		if (heap->type == REGION_HEAP) {
			break;
		}
	}

	if (heap == NULL) {
		// No heap!?
		dprintf(0, "!!! sos_more_heap: no heap region found!\n");
		return 0;
	}

	// Top of heap is the (new) start of the free region, this is
	// what morecore/malloc expect.
	*base = heap->base + heap->size;

	// Move the heap region so SOS knows about it.
	dprintf(3, "*** sos_moremem: was %p %lx\n", heap->base, heap->size);
	heap->size += nb;
	dprintf(3, "*** sos_moremem: now %p %lx\n", heap->base, heap->size);

	// Have the option of returning 0 to signify no more memory.
	return 1;
}

int sos_memuse(void) {
	return FRAME_ALLOC_LIMIT - allocLimit;
}

static Region* findRegion(Region *regions, L4_Word_t addr) {
	Region *r;

	for (r = regions; r != NULL; r = r->next) {
		if (addr >= r->base && addr < r->base + r->size) {
			break;
		} else {
			dprintf(3, "*** findRegion: %p not %p - %p (%d)\n",
					addr, r->base, r->base + r->size, r->type);
		}
	}

	return r;
}

static PagerRequest *newPagerRequest(
		Process *p, L4_Word_t addr, void (*callback)(PagerRequest *pr)) {
	PagerRequest *newPr = (PagerRequest*) malloc(sizeof(PagerRequest));

	newPr->p = p;
	newPr->addr = addr;
	newPr->offset = 0;
	newPr->callback = callback;
	newPr->next = NULL;

	return newPr;
}

static void pagerContinue(PagerRequest *pr) {
	dprintf(2, "*** pagerContinue: replying to %d\n", process_get_pid(pr->p));

	L4_ThreadId_t replyTo = process_get_tid(pr->p);
	free(pr);
	pagerReply(replyTo);
}

static void pager(PagerRequest *pr) {
	L4_Word_t addr = pr->addr & PAGEALIGN;
	L4_Word_t frame;
	L4_Word_t *entry;

	dprintf(2, "*** pager: fault on ss=%d, addr=%p (%p)\n",
			L4_SpaceNo(L4_SenderSpace()), pr->addr, addr);

	// Find region it belongs in.
	dprintf(3, "*** pager: finding region\n");
	Region *r = findRegion(process_get_regions(pr->p), addr);
	dprintf(3, "*** pager: found region %p\n", r);

	if (r == NULL) {
		printf("Segmentation fault\n");
		process_kill(pr->p);
		return;
	}

	// Place in, or retrieve from, page table.
	dprintf(3, "*** pager: finding entry\n");
	entry = pagetableLookup(process_get_pagetable(pr->p), addr);
	frame = *entry & ADDRESS_MASK;
	dprintf(3, "*** pager: entry %p found at %p\n", (void*) *entry, entry);

	if (*entry & ONDISK_MASK) {
		// on disk, queue a swapin request
		dprintf(2, "*** pager: page is on disk\n");
		queueRequest(pr);
		return;
	} else if ((frame & ADDRESS_MASK) != 0) {
		// Already appears in page table as a frame, just got unmapped
		dprintf(3, "*** pager: got unmapped\n");
	} else if (r->mapDirectly) {
		// Wants to be mapped directly (code/data probably).
		dprintf(3, "*** pager: mapping directly\n");
		frame = addr;
		*entry = addr;
	} else {
		// Didn't appear in frame table so we need to allocate a new one.
		// However there are potentially no free frames.
		dprintf(3, "*** pager: allocating frame\n");

		frame = pagerFrameAlloc(pr->p, addr);
		assert((frame & ~ADDRESS_MASK) == 0); // no flags set

		if (frame == 0) {
			dprintf(2, "*** pager: no free frames\n");
			queueRequest(pr);
			return;
		}
	}

	*entry |= REFBIT_MASK;

	dprintf(3, "*** pager: mapping vaddr=%p pid=%d frame=%p rights=%d\n",
			(void*) addr, process_get_pid(pr->p), (void*) frame, r->rights);

	mapPage(process_get_sid(pr->p), addr, frame, r->rights);
	prepareDataOut(pr->p, addr);
	//please(L4_CacheFlushRangeInvalidate(process_get_sid(pr->p),
	//				addr, addr + PAGESIZE));

	dprintf(3, "*** pager: finished, running callback\n");
	pr->callback(pr);
}

static void copyInPrepare(L4_ThreadId_t tid, void *src, size_t size,
		int append) {
	dprintf(3, "*** copyInPrepare: tid=%ld src=%p size=%d\n",
			L4_ThreadNo(tid), src, size);

	L4_Word_t data = copyInOutData[L4_ThreadNo(tid)];
	int newSize = LO_HALF(data);
	int base = HI_HALF(data);

	if (append) {
		newSize += size;
	} else {
		newSize = size;
		base = 0;
	}

	data = LO_HALF(newSize) | (LO_HALF(base) << 16);
	copyInOutData[L4_ThreadNo(tid)] = data;
}

static void copyOutPrepare(L4_ThreadId_t tid, void *dest, size_t size,
		int append) {
	dprintf(3, "*** copyOutPrepare: tid=%ld dest=%p size=%d\n",
			L4_ThreadNo(tid), dest, size);

	L4_Word_t data = copyInOutData[L4_ThreadNo(tid)];
	int newSize = LO_HALF(data);
	int base = HI_HALF(data);

	if (append) {
		newSize += size;
	} else {
		newSize = size;
		base = 0;
	}

	data = LO_HALF(newSize) | (LO_HALF(base) << 16);
	copyInOutData[L4_ThreadNo(tid)] = data;
}

static void writeNonblocking(fildes_t file, size_t nbyte) {
	dprintf(1, "*** writeNonblocking file=%d nbyte=%d\n", file, nbyte);
	L4_Msg_t msg;

	// the actual buffer will be the normal copyin buffer
	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);

	syscall(L4_rootserver, SOS_WRITE, NO_REPLY, &msg);
}

static void readNonblocking(fildes_t file, size_t nbyte) {
	dprintf(1, "*** readNonblocking file=%d nbyte=%d\n", file, nbyte);
	L4_Msg_t msg;
	int rval;

	// the actual buffer will be the normal copyin buffer
	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);

	rval = syscall(L4_rootserver, SOS_READ, NO_REPLY, &msg);
}

static void lseekNonblocking(fildes_t file, int offset, int whence) {
	dprintf(1, "*** lseekNonblocking: file=%d offset=0x%x\n", file, offset);
	L4_Msg_t msg;

	// the actual buffer will be the normal copyin buffer
	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) offset);
	L4_MsgAppendWord(&msg, (L4_Word_t) whence);

	syscall(L4_rootserver, SOS_LSEEK, NO_REPLY, &msg);
}

static void startSwapout(void) {
	dprintf(1, "*** startSwapout\n");
	assert(swapoutRequest.p == NULL);

	// Choose the next page to swap out
	FrameList *swapout = deleteFrameList();
	L4_Word_t *entry = pagetableLookup(
			process_get_pagetable(swapout->p), swapout->page);
	L4_Word_t frame = *entry & ADDRESS_MASK;

	// Make sure the frame reflects what is stored in the frame
	assert((swapout->page & ~PAGEALIGN) == 0);
	prepareDataIn(swapout->p, swapout->page);

	//please(L4_CacheFlushRange(process_get_sid(swapout->p),
	//		swapout->page, swapout->page + PAGESIZE));

	// Tell L4 that the page is no longer backed
	unmapPage(process_get_sid(swapout->p), swapout->page & ADDRESS_MASK);

	// Our own mapping is no longer valid
	//please(L4_CacheFlushRangeInvalidate(L4_rootspace,
	//			frame, frame + PAGESIZE));

	dprintf(1, "*** startSwapout: page=%p addr=%p for pid=%d was %p\n",
			(void*) swapout->page,
			(void*) (swapout->page & ADDRESS_MASK),
			process_get_pid(swapout->p),
			(void*) frame);

	if (verbose > 2) memdump((char*) frame, PAGESIZE);

	// Set up the swapout
	swapoutRequest.p = swapout->p;
	swapoutRequest.addr = frame;
	swapoutRequest.offset = 0;
	swapoutRequest.callback = NULL;
	swapoutRequest.next = NULL;

	// Set up where on disk to put the page
	L4_Word_t diskAddr = swapslot_alloc();
	assert((diskAddr & ~PAGEALIGN) == 0);
	dprintf(1, "*** startSwapout: swapslot is 0x%08lx\n", diskAddr);

	*entry |= ONDISK_MASK;
	*entry &= ~ADDRESS_MASK;
	*entry |= diskAddr;

	// Kick-start with an lseek, the pager message handler loop
	// will start writing from then on
	lseekNonblocking(swapfile, diskAddr, SEEK_SET);
	free(swapout);
}

static void startSwapin(void) {
	dprintf(1, "*** startSwapin\n");

	// pin a frame to copy in to (although "pinned" is somewhat of a misnomer
	// since really it's just a temporary frame and nothing is really pinned)
	pinnedFrame = frame_alloc();

	// kick-start the chain of NFS requests and callbacks by lseeking
	// to the position in the swap file the page is (and this is found
	// by ADDR_MASK since it doubles as physical and ondisk memory location)
	L4_Word_t *entry = pagetableLookup(
			process_get_pagetable(requestsHead->p),
			requestsHead->addr & PAGEALIGN);

	lseekNonblocking(swapfile, *entry & ADDRESS_MASK, SEEK_SET);
}

static void startRequest(void) {
	dprintf(1, "*** startRequest\n");

	// this is called when a frame is needed, either to back a page that is
	// on disk, or just because a process has started using a page for the
	// first time
	assert(allocLimit == 0);
	L4_Word_t *entry = pagetableLookup(
			process_get_pagetable(requestsHead->p),
			requestsHead->addr & PAGEALIGN);

	if (*entry & ONDISK_MASK) {
		// need to swap the entry in first
		startSwapin();
		// swapin continuation will pick up from here
	} else {
		// just need to swap something out
		assert(pinnedFrame == 0);
		startSwapout();
	}
}

static void dequeueRequest(void) {
	dprintf(1, "*** dequeueRequest\n");

	PagerRequest *tmp;

	tmp = requestsHead;
	requestsHead = requestsHead->next;
	//free(tmp); // why does this crash?

	if (requestsHead == NULL) {
		dprintf(1, "*** dequeueRequest: no more items\n");
		requestsLast = NULL;
	} else {
		assert(requestsLast != NULL);
		dprintf(1, "*** dequeueRequest: more items\n");
		startRequest();
	}
}

static void queueRequest(PagerRequest *pr) {
	dprintf(1, "*** queueRequest: p=%d addr=%p\n",
			process_get_pid(pr->p), pr->addr);

	if (requestsLast != NULL) {
		assert(requestsHead != NULL);
		dprintf(1, "*** queueRequest: adding to queue\n");
		requestsLast->next = pr;
		requestsLast = pr;
	} else {
		assert(requestsHead == NULL);
		dprintf(1, "*** queueRequest: starting new queue\n");
		requestsLast = pr;
		requestsHead = pr;
		startRequest();
	}
}

static void memzero(char *addr, L4_Word_t size) {
	for (int i = 0; i < size; i++) {
		addr[i] = 0x00;
	}
}

static void finishedSwapout(void) {
	dprintf(1, "*** finishedSwapout\n");

	// Either the swapout was just for a free frame, or it was for
	// a frame with existing contents (in which case there would be
	// a pinned frame with the contents we need) - in either case we
	// now have a frame we can free
	pagerFrameFree(swapoutRequest.addr);

	// pager is now guaranteed to find a page
	// XXX having this here might be the cause of all our troubles
	// XXX it does it's own cache stuff (prepareDataOut)
	pager(requestsHead);

	int addr = requestsHead->addr & PAGEALIGN;
	L4_Word_t *entry = pagetableLookup(
			process_get_pagetable(requestsHead->p), addr);
	L4_Word_t frame = *entry & ADDRESS_MASK;

	if (pinnedFrame != 0) {
		// there is contents we need to copy across
		dprintf(2, "*** finishedSwapout: pinned frame is %p\n", pinnedFrame);
		memcpy((char*) frame, (void*) pinnedFrame, PAGESIZE);
		frame_free(pinnedFrame);
		pinnedFrame = 0;
	} else {
		// zero the frame for debugging, but it may be a good idea anyway
		dprintf(2, "*** zeroing frame %p\n", (void*) frame);
		memzero((char*) frame, PAGESIZE);
	}

	prepareDataOut(requestsHead->p, requestsHead->addr);

	// Make sure the rootserver's changes are reflected on the frame
	//please(L4_CacheFlushRange(L4_rootspace, frame, frame + PAGESIZE));

	// The user's mapping is no longer valid
	//please(L4_CacheFlushRangeInvalidate(process_get_sid(requestsHead->p),
	//		addr, addr + PAGESIZE));

	dprintf(2, "*** finishedSwapout: page=%p addr=%p for pid=%d now %p\n",
			(void*) requestsHead->addr,
			(void*) (requestsHead->addr & ADDRESS_MASK),
			process_get_pid(requestsHead->p),
			(void*) frame);

	if (verbose > 2) memdump((char*) frame, PAGESIZE);

	swapoutRequest.p = NULL;
	dequeueRequest();
}

static void finishedSwapin(void) {
	dprintf(1, "*** finishedSwapin\n");

	// Either there is a frame free which we can immediately copy
	// the fresh page in to, or there isn't in which case we need
	// to swap something out first
	int addr = requestsHead->addr & PAGEALIGN;
	L4_Word_t *entry = pagetableLookup(
			process_get_pagetable(requestsHead->p), addr);

	// In either case the page is no longer on disk
	*entry &= ~ONDISK_MASK;
	*entry &= ~ADDRESS_MASK;

	if (allocLimit > 0) {
		// Pager is guaranteed to find a page
		pager(requestsHead);
		L4_Word_t frame = *entry & ADDRESS_MASK;

		// Copy data from pinned frame to newly allocated frame
		memcpy((void*) frame, (void*) pinnedFrame, PAGESIZE);

		// Fix caches
		prepareDataOut(requestsHead->p, addr);

		// Make sure our changes are reflected
		//please(L4_CacheFlushRange(L4_rootspace, frame, frame + PAGESIZE));

		// The user's mapping is no longer valid
		//please(L4_CacheFlushRangeInvalidate(process_get_sid(requestsHead->p),
		//			addr, addr + PAGESIZE));

		frames_free(pinnedFrame);
		pinnedFrame = 0;
		dequeueRequest();
	} else {
		// Need to swap something out before being able to continue -
		// the swapout process will deal with the pinned frame etc
		startSwapout();
	}
}

static void demandPager(int vfsRval) {
	dprintf(2, "*** demandPager: vfsRval=%d\n", vfsRval);

	if (swapoutRequest.p != NULL) {
		dprintf(3, "*** demandPager: swapout continuation\n");

		// This is part of a swapout continuation
		assert(swapoutRequest.offset <= PAGESIZE);

		if (swapoutRequest.offset == PAGESIZE) {
			// We finished the swapout so use the now-free frame
			// as either a swapin target or a free frame
			finishedSwapout();
		} else {
			// Still swapping out, continue the vfs write
			memcpy(
					pager_buffer(virtual_pager),
					((char*) swapoutRequest.addr) + swapoutRequest.offset,
					SWAP_BUFSIZ);

			writeNonblocking(swapfile, SWAP_BUFSIZ);
			swapoutRequest.offset += SWAP_BUFSIZ; // FIXME use vfsRval
		}
	} else {
		dprintf(3, "*** demandPager: swapin continuation\n");

		// This is part of a swapin continuation
		assert(requestsHead->offset <= PAGESIZE);

		if (requestsHead->offset > 0) {
			// there is data to copy across to the pinned frame
			assert(vfsRval > 0);
			assert((vfsRval % SWAP_BUFSIZ) == 0);
			assert(((requestsHead->offset - vfsRval) % SWAP_BUFSIZ) == 0);

			memcpy(
					((char*) pinnedFrame) + requestsHead->offset - vfsRval,
					pager_buffer(virtual_pager),
					SWAP_BUFSIZ);
		}

		if (requestsHead->offset == PAGESIZE) {
			// we finished the swapin so we can now attempt to
			// give the page to the process that needs it
			finishedSwapin();
		} else {
			// still swapping in
			readNonblocking(swapfile, SWAP_BUFSIZ);
			requestsHead->offset += SWAP_BUFSIZ;
		}
	}
}

void pager_flush(L4_ThreadId_t tid, L4_Msg_t *msgP) {
	// There is actually a magic fpage that we can use to unmap
	// the whole address space - and I assume we're meant to
	// unmap it from the sender space.
	if (!L4_UnmapFpage(L4_SenderSpace(), L4_CompleteAddressSpace)) {
		sos_print_error(L4_ErrorCode());
		printf("!!! pager_flush: failed to unmap complete address space\n");
	}
}

static void virtualPagerHandler(void) {
	virtual_pager = sos_my_tid();
	dprintf(1, "*** virtualPagerHandler: started, tid %ld\n",
			L4_ThreadNo(virtual_pager));

	// Initialise the swap file
	swapfile_init();
	dprintf(1, "*** virtualPagerHandler: swapfile opened at %d\n", swapfile);

	// Accept the pages and signal we've actually started
	L4_Accept(L4_AddAcceptor(L4_UntypedWordsAcceptor, L4_NotifyMsgAcceptor));

	L4_Msg_t msg;
	L4_MsgTag_t tag;
	L4_ThreadId_t tid = L4_nilthread;
	Process *p;
	PagerRequest *pr;

	for (;;) {
		tag = L4_Wait(&tid);
		//L4_CacheFlushAll();

		tid = sos_cap2tid(tid);
		p = process_lookup(L4_ThreadNo(tid));
		L4_MsgStore(tag, &msg);

		dprintf(2, "*** virtualPagerHandler: got %s from %d\n",
				syscall_show(TAG_SYSLAB(tag)), process_get_pid(p));

		switch (TAG_SYSLAB(tag)) {
			case L4_PAGEFAULT:
				pr = newPagerRequest(p, L4_MsgWord(&msg, 0), pagerContinue);
				pager(pr);
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
				if (L4_IsThreadEqual(process_get_tid(p), L4_rootserver)) {
					demandPager(L4_MsgWord(&msg, 0));
				} else {
					dprintf(0, "!!! virtualPagerHandler: got reply from user\n");
				}
				break;

			case L4_EXCEPTION:
				dprintf(0, "exception: ip=%lx, sp=%lx\ncpsr=%lx\nexception=%lx, cause=%lx\n\n",
						L4_MsgWord(&msg, 0), L4_MsgWord(&msg, 1), L4_MsgWord(&msg, 2),
						L4_MsgWord(&msg, 3), L4_MsgWord(&msg, 4));
				break;

			default:
				dprintf(0, "!!! virtualPagerHandler: unhandled %s from %d\n",
						syscall_show(TAG_SYSLAB(tag)), L4_SpaceNo(L4_SenderSpace()));
		}

		dprintf(2, "*** virtualPagerHandler: finished %s from %d\n",
				syscall_show(TAG_SYSLAB(tag)), process_get_pid(p));
	}
}

void sos_pager_handler(L4_Word_t addr, L4_Word_t ip) {
	dprintf(3, "*** sos_pager_handler: addr=%p ip=%p sender=%ld\n",
			addr, ip, L4_SpaceNo(L4_SenderSpace()));
	addr &= PAGEALIGN;

	if (addr == 0) {
		printf("Segmentation fault in sos_pager_handler\n");
	}

	L4_Fpage_t targetFpage = L4_Fpage(addr, PAGESIZE);
	L4_Set_Rights(&targetFpage, L4_FullyAccessible);
	L4_PhysDesc_t phys = L4_PhysDesc(addr, L4_DefaultMemory);

	if (!L4_MapFpage(L4_SenderSpace(), targetFpage, phys)) { 
		sos_print_error(L4_ErrorCode());
		dprintf(0, "!!! sos_pager: failed at addr %lx ip %lx\n", addr, ip);
	}  
}

char *pager_buffer(L4_ThreadId_t tid) {
	int threadOffset = (L4_ThreadNo(tid) * MAX_IO_BUF);
	return &copyInOutBuffer[threadOffset];
}

static void copyInContinue(PagerRequest *pr) {
	dprintf(2, "*** copyInContinue pr=%p pid=%d addr=%p\n",
			pr, process_get_pid(pr->p), (void*) pr->addr);

	// Data about the copyin operation.
	int threadNum = process_get_pid(pr->p);

	L4_Word_t size = LO_HALF(copyInOutData[threadNum]);
	L4_Word_t offset = HI_HALF(copyInOutData[threadNum]);

	// Continue copying in from where we left off
	char *dest = pager_buffer(process_get_tid(pr->p)) + offset;
	dprintf(3, "*** copyInContinue: size=%ld offset=%ld dest=%p\n",
			size, offset, dest);

	// Assume it's already there - this function should only get
	// called as a continutation from the pager, so no problem
	char *src = (char*) (*pagetableLookup(process_get_pagetable(pr->p),
			(pr->addr & PAGEALIGN)) & ADDRESS_MASK);
	dprintf(3, "*** copyInContinue: frame=%p\n", src);

	src += pr->addr & ~PAGEALIGN;
	dprintf(3, "*** copyInContinue: src=%p\n", src);

	// Start the copy - if we reach a page boundary then pause computation
	// since we can assume that it goes out to disk and waits a long time
	dprintf(4, "*** copyInContinue contents: ");
	while ((offset < size) && (offset < MAX_IO_BUF)) {
		dprintf(4, "%02x->%02x ", *dest & 0x000000ff, *src & 0x000000ff);
		*dest = *src;

		dest++;
		src++;
		offset++;

		if (isPageAligned(src)) break; // continue from next page boundary
	}
	dprintf(4, "\n");

	copyInOutData[threadNum] = size | (offset << 16);
	assert(offset < MAX_IO_BUF); // for testing purposes

	// Reached end - either we finished the copy, or we reached a boundary
	if ((offset >= size) || (offset >= MAX_IO_BUF)) {
		L4_ThreadId_t replyTo = process_get_tid(pr->p);
		free(pr);
		dprintf(3, "*** copyInContinue: finished\n");
		pagerReply(replyTo);
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

	prepareDataIn(p, (L4_Word_t) src);
	copyInPrepare(tid, src, size, append);
	pager(newPagerRequest(p, (L4_Word_t) src, copyInContinue));
}

static void copyOutContinue(PagerRequest *pr) {
	dprintf(3, "*** copyOutContinue pr=%p pid=%ld addr=%p\n",
			pr, process_get_pid(pr->p), (void*) pr->addr);

	// Data about the copyout operation.
	int threadNum = process_get_pid(pr->p);

	L4_Word_t size = LO_HALF(copyInOutData[threadNum]);
	L4_Word_t offset = HI_HALF(copyInOutData[threadNum]);

	// Continue copying out from where we left off
	char *src = pager_buffer(process_get_tid(pr->p)) + offset;
	dprintf(3, "*** copyOutContinue: size=%ld offset=%ld src=%p\n",
			size, offset, src);

	char *dest = (char*) (*pagetableLookup(process_get_pagetable(pr->p),
				(pr->addr & PAGEALIGN)) & ADDRESS_MASK);

	dest += pr->addr & ~PAGEALIGN;
	dprintf(3, "*** copyOutContinue: dest=%p\n", dest);

	dprintf(4, "*** copyOutContinue contents: ");
	while ((offset < size) && (offset < MAX_IO_BUF)) {
		dprintf(4, "%02x->%02x ", *dest & 0x000000ff, *src & 0x000000ff);
		*dest = *src;
		dest++;
		src++;
		offset++;

		if (isPageAligned(dest)) break; // continue from next page boundary
	}

	dprintf(4, "\n");
	copyInOutData[threadNum] = size | (offset << 16);

	assert(offset < MAX_IO_BUF); // for testing purposes

	prepareDataOut(pr->p, pr->addr);

	if ((offset >= size) || (offset >= MAX_IO_BUF)) {
		L4_ThreadId_t replyTo = process_get_tid(pr->p);
		free(pr);
		dprintf(3, "*** copyOutContinue: finished\n");
		pagerReply(replyTo);
	} else {
		pr->addr = (L4_Word_t) dest;
		dprintf(3, "*** copyOutContinue: continuing\n");
		pager(pr);
	}
}

static void copyOut(L4_ThreadId_t tid, void *dest, size_t size, int append) {
	dprintf(3, "*** copyOut: tid=%ld dest=%p size=%d\n",
			L4_ThreadNo(tid), dest, size);

	copyOutPrepare(tid, dest, size, append);

	pager(newPagerRequest(
				process_lookup(L4_ThreadNo(tid)),
				(L4_Word_t) dest,
				copyOutContinue));
}

