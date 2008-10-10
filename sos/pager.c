#include <sos/sos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "frames.h"
#include "l4.h"
#include "libsos.h"
#include "list.h"
#include "pager.h"
#include "process.h"
#include "region.h"
#include "swapfile.h"
#include "syscall.h"

#define verbose 1

// For (demand and otherwise) paging
#define FRAME_ALLOC_LIMIT 4 // limited by the swapfile size

#define ONDISK_MASK  0x00000001
#define REFBIT_MASK  0x00000002
#define ADDRESS_MASK 0xfffff000

typedef struct PidWord_t PidWord;
struct PidWord_t {
	pid_t pid;
	L4_Word_t word;
};

static int allocLimit;
static List *alloced; // [PidWord]
static List *swapped; // [PidWord]

// Asynchronous pager requests
#define SWAP_BUFSIZ 1024

typedef struct PagerRequest_t PagerRequest;
struct PagerRequest_t {
	pid_t pid;
	L4_Word_t addr;
	int offset;
	void (*callback)(PagerRequest *pr);
};

static List *requests; // [PagerRequest]
static PagerRequest swapoutRequest;
static int swappingOut = 0;
static L4_Word_t pinnedFrame;

static void queueRequest(PagerRequest *pr);

// For the pager process
#define PAGER_STACK_SIZE PAGESIZE // DO NOT CHANGE

static L4_Word_t virtualPagerStack[PAGER_STACK_SIZE];
static L4_ThreadId_t virtualPager; // automatically L4_nilthread
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

typedef struct Pagetable2_t {
	L4_Word_t pages[PAGEWORDS];
} Pagetable2;

typedef struct Pagetable1_t {
	Pagetable2 *pages2[PAGEWORDS];
} Pagetable1;

Pagetable *pagetable_init(void) {
	assert(sizeof(Pagetable1) == PAGESIZE);
	Pagetable1 *pt = (Pagetable1*) frame_alloc();

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

static void pagerFrameFree(L4_Word_t frame) {
	assert((frame & ~PAGEALIGN) == 0);
	frame_free(frame);
	allocLimit++;
}

static int framesFree(void *contents, void *data) {
	PidWord *curr = (PidWord*) contents;
	Process *p = (Process*) data;

	if (curr->pid == process_get_pid(p)) {
		L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), curr->word);
		pagerFrameFree(*entry & ADDRESS_MASK);
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
	// Prepare for some data from a user program to be fiddled with by
	// the pager.  This involves flushing the user programs cache on this
	// address, and invalidating our own cache.
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

static PidWord *deleteAllocList(void) {
	dprintf(1, "*** deleteAllocList\n");

	assert(!list_null(alloced));

	Process *p;
	PidWord *tmp, *found = NULL;
	L4_Word_t *entry;

	// Second-chance algorithm
	while (found == NULL) {
		tmp = (PidWord*) list_unshift(alloced);

		p = process_lookup(tmp->pid);
		assert(p != NULL);
		entry = pagetableLookup(process_get_pagetable(p), tmp->word);

		dprintf(3, "*** deleteAllocList: p=%d page=%p frame=%p\n",
				process_get_pid(p), (void*) tmp->word, 
				(void*) (*entry & ADDRESS_MASK));

		if ((*entry & REFBIT_MASK) == 0) {
			// Not been referenced, this is the frame to swap
			found = tmp;
		} else {
			// Been referenced: clear refbit, unmap to give it a chance
			// of being reset again, and move to back
			*entry &= ~REFBIT_MASK;
			unmapPage(process_get_sid(p), tmp->word);
			list_push(alloced, tmp);
		}
	}

	return found;
}

static PidWord *allocPidWord(pid_t pid, L4_Word_t word) {
	PidWord *pw = (PidWord*) malloc(sizeof(PidWord));
	pw->pid = pid;
	pw->word = word;
	return pw;
}

static L4_Word_t pagerFrameAlloc(Process *p, L4_Word_t page) {
	L4_Word_t frame;

	assert(allocLimit >= 0);

	if (allocLimit == 0) {
		dprintf(1, "*** pagerFrameAlloc: allocLimit reached\n");
		frame = 0;
	} else {
		frame = frame_alloc();
		dprintf(1, "*** pagerFrameAlloc: allocated frame %p\n", frame);
		list_push(alloced, allocPidWord(process_get_pid(p), page));

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
	requests = list_empty();

	// Grab a bunch of frames to use for copyin/copyout
	assert((PAGESIZE % MAX_IO_BUF) == 0);
	int numFrames = ((MAX_THREADS * MAX_IO_BUF) / PAGESIZE);

	copyInOutData = (L4_Word_t*) allocFrames(sizeof(L4_Word_t));
	copyInOutBuffer = (char*) allocFrames(numFrames);

	// Start the real pager process
	Process *pager = process_init();

	process_prepare(pager, RUN_AS_THREAD);
	process_set_ip(pager, (void*) virtualPagerHandler);
	process_set_sp(pager, virtualPagerStack + PAGER_STACK_SIZE - 1);

	process_run(pager, RUN_AS_THREAD);

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
	return FRAME_ALLOC_LIMIT - allocLimit;
}

static int findRegion(void *contents, void *data) {
	Region *r = (Region*) contents;
	L4_Word_t addr = (L4_Word_t) data;

	return ((addr >= region_get_base(r)) &&
			(addr < region_get_base(r) + region_get_size(r)));
}

static PagerRequest *allocPagerRequest(
		pid_t pid, L4_Word_t addr, void (*callback)(PagerRequest *pr)) {
	PagerRequest *newPr = (PagerRequest*) malloc(sizeof(PagerRequest));

	newPr->pid = pid;
	newPr->addr = addr;
	newPr->offset = 0;
	newPr->callback = callback;

	return newPr;
}

static void pagerContinue(PagerRequest *pr) {
	dprintf(2, "*** pagerContinue: replying to %d\n", pr->pid);

	L4_ThreadId_t replyTo = process_get_tid(process_lookup(pr->pid));
	free(pr);
	syscall_reply_m(replyTo, 0);
}

static L4_Word_t pagerSwapslotAlloc(Process *p) {
	L4_Word_t diskAddr = swapslot_alloc();

	if (diskAddr == ADDRESS_NONE) {
		dprintf(0, "!!! pagerSwapslotAlloc: none available\n");
		return ADDRESS_NONE;
	} else {
		list_push(swapped, allocPidWord(process_get_pid(p), diskAddr));
		return diskAddr;
	}
}

static int pagerSwapslotFree(void *contents, void *data) {
	PidWord *curr = (PidWord*) contents;
	PidWord *args = (PidWord*) data;

	if ((curr->pid == args->pid) &&
			((curr->word == args->word) || (curr->word == ADDRESS_ALL))) {
		swapslot_free(curr->word);
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
	PidWord *args;
	int result;

	// Store the address of the PCB before the rootserver hides it
	p = process_lookup(pid);

	// Kill and hide the process
	result = process_kill(p);

	if (result != 0) {
		return result; // error
	}

	// Free all resources
	args = allocPidWord(process_get_pid(p), ADDRESS_ALL);

	list_delete(alloced, framesFree, p);
	list_delete(swapped, pagerSwapslotFree, args);
	pagetableFree(p);
	list_iterate(process_get_regions(p), regionsFree, NULL);

	free(args);

	// Wake all waiting processes
	process_wake_all(process_get_pid(p));

	// And done
	free(p);

	return 0;
}

static int pagerAction(PagerRequest *pr) {
	Process *p;
	L4_Word_t addr, frame, *entry;

	addr = pr->addr & PAGEALIGN;

	dprintf(2, "*** pagerAction: fault on ss=%d, addr=%p (%p)\n",
			L4_SpaceNo(L4_SenderSpace()), pr->addr, addr);

	p = process_lookup(pr->pid);
	if (p == NULL) {
		// Process died
		return 0;
	}

	// Find region it belongs in.
	dprintf(3, "*** pagerAction: finding region\n");
	Region *r = list_find(process_get_regions(p), findRegion, (void*) addr);

	if (r == NULL) {
		printf("Segmentation fault\n");
		processDelete(process_get_pid(p));
		return 0;
	}

	// Place in, or retrieve from, page table.
	dprintf(3, "*** pagerAction: finding entry\n");
	entry = pagetableLookup(process_get_pagetable(p), addr);
	frame = *entry & ADDRESS_MASK;
	dprintf(3, "*** pagerAction: entry %p found at %p\n", (void*) *entry, entry);

	if (*entry & ONDISK_MASK) {
		// On disk, queue a swapin request
		dprintf(2, "*** pagerAction: page is on disk\n");
		queueRequest(pr);
		return 0;
	} else if ((frame & ADDRESS_MASK) != 0) {
		// Already appears in page table as a frame, just got unmapped
		// (probably to update the refbit)
		dprintf(3, "*** pagerAction: got unmapped\n");
	} else if (region_map_directly(r)) {
		// Wants to be mapped directly (code/data probably).
		dprintf(3, "*** pagerAction: mapping directly\n");
		frame = addr;
	} else {
		// Didn't appear in frame table so we need to allocate a new one.
		// However there are potentially no free frames.
		dprintf(3, "*** pagerAction: allocating frame\n");

		frame = pagerFrameAlloc(p, addr);
		assert((frame & ~ADDRESS_MASK) == 0); // no flags set

		if (frame == 0) {
			dprintf(2, "*** pagerAction: no free frames\n");
			queueRequest(pr);
			return 0;
		}
	}

	*entry = frame | REFBIT_MASK;

	dprintf(3, "*** pagerAction: mapping vaddr=%p pid=%d frame=%p rights=%d\n",
			(void*) addr, process_get_pid(p), (void*) frame, region_get_rights(r));
	mapPage(process_get_sid(p), addr, frame, region_get_rights(r));

	return 1;
}

static void pager(PagerRequest *pr) {
	if (pagerAction(pr)) {
		pr->callback(pr);
	} else {
		dprintf(3, "*** pager: pagerAction stalled\n");
	}
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
	dprintf(2, "*** startSwapout\n");

	Process *p;
	PidWord *swapout;
	L4_Word_t *entry, frame, addr;

	// Choose the next page to swap out
	swapout = deleteAllocList();
	p = process_lookup(swapout->pid);
	assert(p != NULL);

	entry = pagetableLookup(process_get_pagetable(p), swapout->word);
	frame = *entry & ADDRESS_MASK;

	// Make sure the frame reflects what is stored in the frame
	assert((swapout->word & ~PAGEALIGN) == 0);
	addr = swapout->word & PAGEALIGN;
	prepareDataIn(p, addr);

	// The page is no longer backed
	unmapPage(process_get_sid(p), addr);

	dprintf(1, "*** startSwapout: page=%p addr=%p for pid=%d was %p\n",
			(void*) swapout->word, (void*) addr,
			process_get_pid(p), (void*) frame);

	// Set up the swapout
	swapoutRequest.pid = swapout->pid;
	swapoutRequest.addr = frame;
	swapoutRequest.offset = 0;
	swapoutRequest.callback = NULL;
	swappingOut = 1;

	// Set up where on disk to put the page
	L4_Word_t diskAddr = pagerSwapslotAlloc(p);
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
	dprintf(2, "*** startSwapin\n");

	// pin a frame to copy in to (although "pinned" is somewhat of a misnomer
	// since really it's just a temporary frame and nothing is really pinned)
	pinnedFrame = frame_alloc();

	// kick-start the chain of NFS requests and callbacks by lseeking
	// to the position in the swap file the page is (and this is found
	// by ADDR_MASK since it doubles as physical and ondisk memory location)
	PagerRequest *nextPr = (PagerRequest*) list_peek(requests);

	L4_Word_t *entry = pagetableLookup(
			process_get_pagetable(process_lookup(nextPr->pid)),
			nextPr->addr & PAGEALIGN);

	lseekNonblocking(swapfile, *entry & ADDRESS_MASK, SEEK_SET);
}

static void startRequest(void) {
	dprintf(1, "*** startRequest\n");
	assert(!list_null(requests));

	// This is called to start running the next pager request, assuming
	// there is another one 
	PagerRequest *nextPr = (PagerRequest*) list_peek(requests);

	L4_Word_t *entry = pagetableLookup(
			process_get_pagetable(process_lookup(nextPr->pid)),
			nextPr->addr & PAGEALIGN);

	if (*entry & ONDISK_MASK) {
		// At the very least a page needs to be swapped in first
		startSwapin();
		// Afterwards, may have to swap something out
	} else {
		// Needed to swap something out
		assert(pinnedFrame == 0);

		if (allocLimit > 0) {
			// In the meantime a frame has become free
			pager((PagerRequest*) list_unshift(requests));
		} else {
			startSwapout();
		}
	}
}

static void dequeueRequest(void) {
	dprintf(1, "*** dequeueRequest\n");

	list_unshift(requests);

	if (list_null(requests)) {
		dprintf(1, "*** dequeueRequest: no more items\n");
	} else {
		dprintf(1, "*** dequeueRequest: running next\n");
		startRequest();
	}
}

static void queueRequest(PagerRequest *pr) {
	dprintf(1, "*** queueRequest: p=%d addr=%p\n", pr->pid, pr->addr);

	if (list_null(requests)) {
		list_push(requests, pr);
		startRequest();
	} else {
		list_push(requests, pr);
	}
}

static void memzero(char *addr, L4_Word_t size) {
	for (int i = 0; i < size; i++) addr[i] = 0x00;
}

static void finishedSwapout(void) {
	dprintf(1, "*** finishedSwapout\n");

	Process *p;
	PagerRequest *request;
	L4_Word_t *entry, frame, addr;

	// Either the swapout was just for a free frame, or it was for
	// a frame with existing contents (in which case there would be
	// a pinned frame with the contents we need) - in either case we
	// now have a frame we can free
	pagerFrameFree(swapoutRequest.addr);
	swappingOut = 0;

	request = (PagerRequest*) list_peek(requests);
	p = process_lookup(request->pid);

	if (p == NULL) {
		// Process died
		request->callback(request);
		dequeueRequest();
	}

	// Pager is now guaranteed to find a page (note that the pager
	// action has been separated, we reply later)
	pagerAction(request);

	addr = request->addr & PAGEALIGN;
	entry = pagetableLookup(process_get_pagetable(p), addr);
	frame = *entry & ADDRESS_MASK;

	if (pinnedFrame != 0) {
		// There is contents we need to copy across
		dprintf(2, "*** finishedSwapout: pinned frame is %p\n", pinnedFrame);
		memcpy((char*) frame, (void*) pinnedFrame, PAGESIZE);
		frame_free(pinnedFrame);
		pinnedFrame = 0;
	} else {
		// Zero the frame for debugging, but it may be a good idea anyway
		dprintf(2, "*** zeroing frame %p\n", (void*) frame);
		memzero((char*) frame, PAGESIZE);
	}

	dprintf(2, "*** finishedSwapout: addr=%p for pid=%d now %p\n",
			(void*) addr, process_get_pid(p), (void*) frame);

	prepareDataOut(p, addr);
	request->callback(request);
	dequeueRequest();
}

static void finishedSwapin(void) {
	dprintf(1, "*** finishedSwapin\n");
	Process *p;
	PagerRequest *request;
	PidWord *args;
	L4_Word_t *entry, addr;

	request = (PagerRequest*) list_peek(requests);
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
	assert(*entry & ONDISK_MASK);

	args = allocPidWord(process_get_pid(p), *entry & ADDRESS_MASK);
	list_delete(swapped, pagerSwapslotFree, args);
	free(args);
	*entry &= ~ONDISK_MASK;
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

static void demandPager(int vfsRval) {
	dprintf(2, "*** demandPager: vfsRval=%d\n", vfsRval);

	if (swappingOut) {
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
					pager_buffer(virtualPager),
					((char*) swapoutRequest.addr) + swapoutRequest.offset,
					SWAP_BUFSIZ);

			writeNonblocking(swapfile, SWAP_BUFSIZ);
			swapoutRequest.offset += SWAP_BUFSIZ; // FIXME use vfsRval
		}
	} else {
		dprintf(3, "*** demandPager: swapin continuation\n");

		// This is part of a swapin continuation
		PagerRequest *request = (PagerRequest*) list_peek(requests);
		assert(request->offset <= PAGESIZE);

		if (request->offset > 0) {
			// there is data to copy across to the pinned frame
			assert(vfsRval > 0);
			assert((vfsRval % SWAP_BUFSIZ) == 0);
			assert(((request->offset - vfsRval) % SWAP_BUFSIZ) == 0);

			memcpy(
					((char*) pinnedFrame) + request->offset - vfsRval,
					pager_buffer(virtualPager),
					SWAP_BUFSIZ);
		}

		if (request->offset == PAGESIZE) {
			// we finished the swapin so we can now attempt to
			// give the page to the process that needs it
			finishedSwapin();
		} else {
			// still swapping in
			readNonblocking(swapfile, SWAP_BUFSIZ);
			request->offset += SWAP_BUFSIZ;
		}
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

	swapfile_init();
	dprintf(1, "*** virtualPagerHandler: swapfile=%d\n", swapfile);

	L4_Msg_t msg;
	L4_MsgTag_t tag;
	L4_ThreadId_t tid = L4_nilthread;
	Process *p;
	L4_Word_t tmp;

	for (;;) {
		tag = L4_Wait(&tid);

		tid = sos_cap2tid(tid);
		p = process_lookup(L4_ThreadNo(tid));
		L4_MsgStore(tag, &msg);

		dprintf(2, "*** virtualPagerHandler: got %s from %d\n",
				syscall_show(TAG_SYSLAB(tag)), process_get_pid(p));

		switch (TAG_SYSLAB(tag)) {
			case L4_PAGEFAULT:
				pager(allocPagerRequest(process_get_pid(p),
							L4_MsgWord(&msg, 0), pagerContinue));
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
				syscall_reply(tid, swapfile_usage());
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
				syscall_reply(tid, processDelete(L4_MsgWord(&msg, 0)));
				break;

			case SOS_DEBUG_FLUSH:
				pagerFlush();
				syscall_reply_m(tid, 0);
				break;

			case L4_EXCEPTION:
				dprintf(0, "!!! virtualPagerHandler exception: ip=%lx, sp=%lx\n",
						L4_MsgWord(&msg, 0), L4_MsgWord(&msg, 1));
				dprintf(0, "    cpsr=%lx exception=%lx, cause=%lx\n",
						L4_MsgWord(&msg, 2), L4_MsgWord(&msg, 3), L4_MsgWord(&msg, 4));
				break;

			default:
				dprintf(0, "!!! pager: unhandled syscall tid=%ld id=%d name=%s\n",
						L4_ThreadNo(tid), TAG_SYSLAB(tag), syscall_show(TAG_SYSLAB(tag)));
		}

		dprintf(2, "*** virtualPagerHandler: finished %s from %d\n",
				syscall_show(TAG_SYSLAB(tag)), process_get_pid(p));
	}
}

char *pager_buffer(L4_ThreadId_t tid) {
	return &copyInOutBuffer[L4_ThreadNo(tid) * MAX_IO_BUF];
}

static void copyInContinue(PagerRequest *pr) {
	dprintf(2, "*** copyInContinue pr=%p pid=%d addr=%p\n",
			pr, pr->pid, (void*) pr->addr);

	Process *p = process_lookup(pr->pid);
	L4_Word_t addr = pr->addr & PAGEALIGN;

	// Prepare caches
	prepareDataIn(p, addr);

	// Data about the copyin operation.
	L4_Word_t size = LO_HALF(copyInOutData[process_get_pid(p)]);
	L4_Word_t offset = HI_HALF(copyInOutData[process_get_pid(p)]);

	// Continue copying in from where we left off
	char *dest = pager_buffer(process_get_tid(p)) + offset;
	dprintf(3, "*** copyInContinue: size=%ld offset=%ld dest=%p\n",
			size, offset, dest);

	// Assume it's already there - this function should only get
	// called as a continutation from the pager, so no problem
	char *src = (char*) (*pagetableLookup(process_get_pagetable(p), addr)
			& ADDRESS_MASK);
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

	copyInOutData[process_get_pid(p)] = size | (offset << 16);
	assert(offset < MAX_IO_BUF); // XXX silently fail?

	// Reached end - either we finished the copy, or we reached a boundary
	if ((offset >= size) || (offset >= MAX_IO_BUF)) {
		L4_ThreadId_t tid = process_get_tid(p);
		free(pr);
		dprintf(3, "*** copyInContinue: finished\n");
		syscall_reply_m(tid, 0);
	} else {
		pr->addr = (L4_Word_t) src;
		dprintf(3, "*** copyInContinue: continuing\n");
		pager(pr);
	}
}

static void copyIn(L4_ThreadId_t tid, void *src, size_t size, int append) {
	dprintf(3, "*** copyIn: tid=%ld src=%p size=%d\n",
			L4_ThreadNo(tid), src, size);

	copyInPrepare(tid, src, size, append);

	pager(allocPagerRequest(
				process_get_pid(process_lookup(L4_ThreadNo(tid))),
				(L4_Word_t) src,
				copyInContinue));
}

static void copyOutContinue(PagerRequest *pr) {
	dprintf(3, "*** copyOutContinue pr=%p pid=%ld addr=%p\n",
			pr, pr->pid, (void*) pr->addr);

	Process *p = process_lookup(pr->pid);
	L4_Word_t addr = pr->addr & PAGEALIGN;

	// Data about the copyout operation.
	L4_Word_t size = LO_HALF(copyInOutData[process_get_pid(p)]);
	L4_Word_t offset = HI_HALF(copyInOutData[process_get_pid(p)]);

	// Continue copying out from where we left off
	char *src = pager_buffer(process_get_tid(p)) + offset;
	dprintf(3, "*** copyOutContinue: size=%ld offset=%ld src=%p\n",
			size, offset, src);

	char *dest = (char*) (*pagetableLookup(process_get_pagetable(p), addr)
				& ADDRESS_MASK);
	dest += pr->addr & ~PAGEALIGN;
	dprintf(3, "*** copyOutContinue: dest=%p\n", dest);

	// Start the copy, same story as copyin
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

	copyInOutData[process_get_pid(p)] = size | (offset << 16);
	assert(offset < MAX_IO_BUF); // XXX silently fail?

	prepareDataOut(p, addr);

	if ((offset >= size) || (offset >= MAX_IO_BUF)) {
		L4_ThreadId_t tid = process_get_tid(p);
		free(pr);
		dprintf(3, "*** copyOutContinue: finished\n");
		syscall_reply_m(tid, 0);
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

	pager(allocPagerRequest(
				process_get_pid(process_lookup(L4_ThreadNo(tid))),
				(L4_Word_t) dest,
				copyOutContinue));
}

