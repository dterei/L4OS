#include <sos/sos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frames.h"
#include "l4.h"
#include "libsos.h"
#include "pager.h"
#include "process.h"
#include "swapfile.h"
#include "syscall.h"

#define verbose 2

// For (demand and otherwise) paging
#define FRAME_ALLOC_LIMIT 1024 // limited by the swapfile size

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
typedef enum {
	SWAP_OUT,
	SWAP_NO,
	SWAP_YES
} swap_t;

#define SWAP_BUFSIZ 256

struct PagerRequest_t {
	Process *p;
	L4_Word_t addr;
	swap_t type;
	int offset;
	void (*callback)(PagerRequest *pr);
	PagerRequest *next;
};

static PagerRequest *swapinRequestsHead = NULL;
static PagerRequest *swapinRequestsLast = NULL;
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

// Page table structures
// Each level of the page table is assumed to be of size PAGESIZE
typedef struct PageTable2_t {
	L4_Word_t pages[PAGEWORDS];
} PageTable2;

typedef struct PageTable1_t {
	PageTable2 *pages2[PAGEWORDS];
} PageTable1;

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

PageTable *pagetable_init(void) {
	assert(sizeof(PageTable1) == PAGESIZE);
	PageTable1 *pt = (PageTable1*) frame_alloc();

	for (int i = 0; i < PAGEWORDS; i++) {
		pt->pages2[i] = NULL;
	}

	return (PageTable*) pt;
}

static L4_Word_t*
pageTableLookup(PageTable *pt, L4_Word_t addr) {
	PageTable1 *level1 = (PageTable1*) pt;

	addr /= PAGESIZE;
	int offset1 = addr / PAGEWORDS;
	int offset2 = addr - (offset1 * PAGEWORDS);

	if (level1 == NULL) {
		dprintf(0, "!!! pageTableLookup: level1 is NULL!\n");
		return NULL;
	} else if (level1->pages2 == NULL) {
		dprintf(0, "!!! pageTableLookup: level1->pages2 is NULL!\n");
		return NULL;
	}

	if (level1->pages2[offset1] == NULL) {
		assert(sizeof(PageTable2) == PAGESIZE);
		level1->pages2[offset1] = (PageTable2*) frame_alloc();

		for (int i = 0; i < PAGEWORDS; i++) {
			level1->pages2[offset1]->pages[i] = 0;
		}
	}

	return &level1->pages2[offset1]->pages[offset2];
}

static void pagerFrameFree(L4_Word_t frame) {
	frame_free(frame);
	allocLimit++;
}

void pagetable_free(PageTable *pt) {
	PageTable1 *pt1 = (PageTable1*) pt;

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

static void addFrameList(Process *p, L4_Word_t page) {
	FrameList *new = (FrameList*) malloc(sizeof(FrameList));

	new->p = p;
	new->page = page;

	if (allocLast == NULL) {
		allocLast = new->next;
		assert(allocHead == NULL);
		allocHead = new->next;
	} else {
		new->next = NULL;
		allocLast->next = new;
	}
}

static FrameList *deleteFrameList(void) {
	assert(allocHead != NULL);
	assert(allocLast != NULL);

	FrameList *tmp;
	FrameList *found = NULL;

	// Second-chance algorithm
	while (found != NULL) {
		L4_Word_t *vpage = pageTableLookup(process_get_pagetable(
					allocHead->p), allocHead->page);

		if ((*vpage & REFBIT_MASK) == 0) {
			// Not been referenced, this is the frame to swap
			found = allocHead;
			allocHead = allocHead->next;
		} else {
			// Been referenced, clear...
			*vpage &= ~REFBIT_MASK;

			// and move to back
			tmp = allocHead;
			allocHead = allocHead->next;
			allocLast->next = tmp;
			tmp->next = NULL;
			allocLast = tmp;
		}
	}

	return found;
}

static L4_Word_t pagerFrameAlloc(Process *p, L4_Word_t vaddr) {
	L4_Word_t frame;

	assert(allocLimit >= 0);

	if (allocLimit == 0) {
		dprintf(1, "*** allocLimit reached\n");
		frame = 0;
	} else {
		frame = frame_alloc();
		dprintf(2, "*** allocated frame %p\n", frame);
		process_get_info(p)->size++;
		allocLimit--;

		L4_Word_t *entry = pageTableLookup(process_get_pagetable(p), vaddr);
		*entry = frame | (*entry & ~ADDRESS_MASK);
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

	// Initialise the swap file
	swapfile_init();

	// Start the real pager process
	Process *pager = process_init();

	process_prepare(pager);
	process_set_ip(pager, (void*) virtualPagerHandler);
	process_set_sp(pager, virtualPagerStack + PAGER_STACK_SIZE - 1);

	// Start pager, but wait until it has actually started before
	// trying to assign virtual_pager to anything
	dprintf(1, "*** pager_init: about to run pager\n");
	process_run(pager, RUN_AS_THREAD);
	virtual_pager = process_get_tid(pager);
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

static Region*
findRegion(Region *regions, L4_Word_t addr) {
	Region *r;

	for (r = regions; r != NULL; r = r->next) {
		if (addr >= r->base && addr < r->base + r->size) {
			break;
		} else {
			dprintf(2, "*** findRegion: %p not %p - %p (%d)\n",
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
	dprintf(2, "*** pagerContinue: replying to %ld\n",
			L4_ThreadNo(process_get_tid(pr->p)));

	L4_ThreadId_t replyTo = process_get_tid(pr->p);
	free(pr);
	syscall_reply(replyTo, 0);
}

static void pager(PagerRequest *pr) {
	L4_Word_t frame;
	int rights;
	int mapKernelToo = 0;
	L4_Word_t addr = pr->addr & PAGEALIGN;

	dprintf(2, "*** pager: fault on ss=%d, addr=%p (%p)\n",
			L4_SpaceNo(L4_SenderSpace()), pr->addr, addr);
	assert(!L4_IsSpaceEqual(L4_SenderSpace(), L4_rootspace));

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
	L4_Word_t *entry = pageTableLookup(process_get_pagetable(pr->p), addr);
	L4_Word_t entryAddr = *entry & ADDRESS_MASK;

	dprintf(3, "*** pager: entry found to be %p at %p\n", (void*) *entry, entry);

	if (*entry & ONDISK_MASK) {
		// on disk, queue a swapin request
		dprintf(2, "*** pager: page is on disk\n");
		pr->type = SWAP_YES;
		queueRequest(pr);
		return;
	} else if (entryAddr != 0) {
		// Already appears in page table as a frame, just got unmapped
		dprintf(2, "*** pager: got unmapped\n");
	} else if (r->mapDirectly) {
		// Wants to be mapped directly (code/data probably).
		// In this case the kernel doesn't know about it from the
		// frame table, so map it 1:1 in the kernel too.
		dprintf(2, "*** pager: mapping directly\n");
		entryAddr = *entry = addr;
		mapKernelToo = 1;
	} else {
		// Didn't appear in frame table so we need to allocate a new one.
		// However there are potentially no free frames.
		dprintf(2, "*** pager: allocating frame\n");

		entryAddr = pagerFrameAlloc(pr->p, addr);
		assert((entryAddr & ~ADDRESS_MASK) == 0); // no flags set

		if (entryAddr == 0) {
			// No free frames
			dprintf(2, "*** pager: no free frames\n");
			pr->type = SWAP_NO;
			queueRequest(pr);
			return;
		} else {
			// We're fine
		}
	}

	*entry |= REFBIT_MASK;
	frame = entryAddr;
	rights = r->rights;

	L4_SpaceId_t sid = sos_tid2sid(process_get_tid(pr->p));
	L4_Fpage_t fpage = L4_Fpage(addr, PAGESIZE);
	L4_Set_Rights(&fpage, rights);
	L4_PhysDesc_t ppage = L4_PhysDesc(frame, L4_DefaultMemory);

	dprintf(3, "*** pager: mapping %lx with sid=%lx frame=%ld rights=%d\n",
			frame, sid.raw, frame, rights);

	if (!L4_MapFpage(sid, fpage, ppage)) {
		sos_print_error(L4_ErrorCode());
		dprintf(0, "Can't map page at %lx to frame %lx\n",
				addr, frame);
	}

	// XXX hopefully this will OVERRIDE any existing mappings

	if (mapKernelToo) {
		if (!L4_MapFpage(L4_rootspace, fpage, ppage)) {
			sos_print_error(L4_ErrorCode());
			dprintf(0, "Failed mapping to kernel too\n");
		}
	}

	dprintf(3, "*** pager: finished, waking faulter\n");
	pr->callback(pr);
}

static void copyInPrepare(L4_ThreadId_t tid, void *src, size_t size,
		int append) {
	dprintf(2, "*** copyInPrepare: tid=%ld src=%p size=%d\n",
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
	dprintf(2, "*** copyOutPrepare: tid=%ld dest=%p size=%d\n",
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
	L4_Msg_t msg;

	// the actual buffer will be the normal copyin buffer
	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);

	syscall_run(SOS_WRITE, NO_REPLY, &msg);
}

static void readNonblocking(fildes_t file, size_t nbyte) {
	L4_Msg_t msg;

	// the actual buffer will be the normal copyin buffer
	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) nbyte);

	syscall_run(SOS_READ, NO_REPLY, &msg);
}

static void lseekNonblocking(fildes_t file, int offset, int whence) {
	L4_Msg_t msg;

	// the actual buffer will be the normal copyin buffer
	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) file);
	L4_MsgAppendWord(&msg, (L4_Word_t) offset);
	L4_MsgAppendWord(&msg, (L4_Word_t) whence);

	// XXX
	syscall_run(42, NO_REPLY, &msg);
}

static void queueSwapout(void) {
	assert(swapoutRequest.p == NULL);

	FrameList *swapout = deleteFrameList();
	L4_Word_t *entry = pageTableLookup(process_get_pagetable(swapout->p),
			swapout->page);

	swapoutRequest.p = swapout->p;
	swapoutRequest.addr = *entry & ADDRESS_MASK;
	swapoutRequest.type = SWAP_OUT;
	swapoutRequest.offset = get_swapslot();
	swapoutRequest.callback = NULL;
	swapoutRequest.next = NULL;

	*entry |= ONDISK_MASK;
	*entry &= ~ADDRESS_MASK;

	assert(isPageAligned(&(swapoutRequest.offset)));
	*entry |= swapoutRequest.offset;

	lseekNonblocking(swapfile, swapoutRequest.offset, SEEK_SET);
}

static void startSwapin(void) {
	// We want to swap something in but there might be no frames left
	// to actually do so, so we might have to do a swapout first
	L4_Word_t frame = pagerFrameAlloc(
			swapinRequestsHead->p,
			swapinRequestsHead->addr);

	if (frame != 0) {
		pinnedFrame = frame;
		lseekNonblocking(swapfile, swapinRequestsHead->offset, SEEK_SET);
	} else {
		// need to kick something out
		queueSwapout();
		// this swapin will be called after the swapout finishes
	}
}

static void queueRequest(PagerRequest *pr) {
	if (swapinRequestsLast == NULL) {
		assert(swapinRequestsHead == NULL);
		swapinRequestsLast = swapinRequestsHead = pr;
		startSwapin();
	} else {
		swapinRequestsLast->next = pr;
	}
}

static void finishedSwapout(void) {
	if (swapinRequestsHead->type == SWAP_NO) {
		// the process just needed a new frame, nothing to swap in -
		// the pager should handle everything
		pager(swapinRequestsHead);
	}

	// this will always be true if it was a SWAP_YES
	if (swapinRequestsHead != NULL) {
		startSwapin();
	}

	// delete request
	swapoutRequest.p = NULL;
}

static void finishedSwapin(void) {
	// the contents of the page will now be in the per-tid buffer
	// so copy that to the pinned frame, free the frame, then
	// call pager manually
	assert(MAX_IO_BUF >= PAGESIZE);

	memcpy((void*) pinnedFrame, (void*) pager_buffer(L4_rootserver), PAGESIZE);
	frame_free(pinnedFrame);

	// need to unset the ondisk mask
	L4_Word_t *ptEntry = pageTableLookup(process_get_pagetable(swapinRequestsHead->p),
			swapinRequestsHead->addr);
	*ptEntry &= ~ONDISK_MASK;

	// pager will free the request
	PagerRequest *nextRequest = swapinRequestsHead->next;
	pager(swapinRequestsHead);
	swapinRequestsHead = nextRequest;

	if (swapinRequestsHead == NULL) {
		swapinRequestsLast = NULL;
	} else {
		startSwapin();
	}
}

static void demandPager(int vfsRval) {
	/*
	 * Algorithm:
	 *
	 * If we're swapping out then continue the swap.
	 * If not then we must be swapping in.
	 */

	if (swapoutRequest.p != NULL) {
		// This is part of a swapout continuation

		assert(swapoutRequest.offset <= PAGESIZE);

		if (swapoutRequest.offset == PAGESIZE) {
			// we finished the swapout, use the now-free frame
			// as either a swapin target or a free frame
			finishedSwapout();
		} else {
			// still swapping out, continue the vfs write
			// need to copyin the data manually, don't need
			// system call though thankfully since we can
			// just copy in normally.
			// note that this will go in to SOS's copyin
			// buffer but that will work nicely
			writeNonblocking(swapfile, SWAP_BUFSIZ);
			swapoutRequest.offset += SWAP_BUFSIZ;
		}
	} else {
		// This is part of a swapin continuation

		assert(swapinRequestsHead->offset <= PAGESIZE);

		if (swapinRequestsHead->offset == PAGESIZE) {
			// we finished the swapin so we can wake up the
			// process now via the continuation and then
			// service the next request
			finishedSwapin();
		} else {
			// still swapping in, continue the vfs read
			readNonblocking(swapfile, SWAP_BUFSIZ);
			swapinRequestsHead->offset += SWAP_BUFSIZ;
		}
	}
}

void
pager_flush(L4_ThreadId_t tid, L4_Msg_t *msgP) {
	// There is actually a magic fpage that we can use to unmap
	// the whole address space - and I assume we're meant to
	// unmap it from the sender space.
	if (!L4_UnmapFpage(L4_SenderSpace(), L4_CompleteAddressSpace)) {
		sos_print_error(L4_ErrorCode());
		printf("!!! pager_flush: failed to unmap complete address space\n");
	}
}

static void virtualPagerHandler(void) {
	dprintf(1, "*** virtualPagerHandler: started\n");

	// Accept the pages and signal we've actually started
	L4_Accept(L4_AddAcceptor(L4_UntypedWordsAcceptor, L4_NotifyMsgAcceptor));

	L4_Msg_t msg;
	L4_MsgTag_t tag;
	L4_ThreadId_t tid = L4_nilthread;
	Process *p;
	PagerRequest *pr;

	for (;;) {
		tag = L4_Wait(&tid);
		p = process_lookup(L4_SpaceNo(L4_SenderSpace()));
		L4_MsgStore(tag, &msg);

		dprintf(2, "*** virtualPagerHandler: got request from %ld\n",
				L4_ThreadNo(process_get_tid(p)));

		switch (TAG_SYSLAB(tag)) {
			case L4_PAGEFAULT:
				pr = newPagerRequest(p, L4_MsgWord(&msg, 0), pagerContinue);
				pager(pr);
				break;

			case SOS_REPLY:
				dprintf(1, "*** virtualPagerHandler: got reply\n");
				if (L4_IsSpaceEqual(L4_SenderSpace(), L4_rootspace)) {
					demandPager(L4_MsgWord(&msg, 0));
				} else {
					dprintf(0, "!!! virtualPagerHandler: got reply from user\n");
				}
				break;

			default:
				dprintf(0, "!!! virtualPagerHandler: unhandled %s from %d\n",
						syscall_show(TAG_SYSLAB(tag)), L4_SpaceNo(L4_SenderSpace()));
		}
	}
}

void sos_pager_handler(L4_Word_t addr, L4_Word_t ip) {
	dprintf(2, "*** sos_pager_handler: addr=%p ip=%p sender=%ld\n",
			addr, ip, L4_SpaceNo(L4_SenderSpace()));
	addr &= PAGEALIGN;

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
	dprintf(2, "*** copyInContinue pr=%p process=%p tid=%ld addr=%p\n",
			pr, pr->p, process_get_tid(pr->p), (void*) pr->addr);

	// Data about the copyin operation.
	int threadNum = L4_ThreadNo(process_get_tid(pr->p));

	L4_Word_t size = copyInOutData[threadNum] & 0x0000ffff;
	L4_Word_t offset = (copyInOutData[threadNum] >> 16) & 0x0000ffff;

	// Continue copying in from where we left off
	char *dest = pager_buffer(process_get_tid(pr->p)) + offset;
	dprintf(3, "*** copyInContinue: size=%ld offset=%ld dest=%p\n",
			size, offset, dest);

	// Assume it's already there - this function should only get
	// called as a continutation from the pager, so no problem
	char *src = (char*) (*pageTableLookup(
			process_get_pagetable(pr->p), pr->addr) & ADDRESS_MASK);

	src += pr->addr & (PAGESIZE - 1);
	dprintf(3, "*** copyInContinue: src=%p\n", src);

	// Start the copy - if we reach a page boundary then pause computation
	// since we can assume that it goes out to disk and waits a long time
	while ((offset < size) && (offset < MAX_IO_BUF)) {
		*dest = *src;
		dest++;
		src++;
		offset++;

		if (isPageAligned(src)) break; // continue from next page boundary
	}

	copyInOutData[threadNum] = size | (offset << 16);

	// Reached end - either we finished the copy, or we reached a boundary
	if (offset >= size) {
		L4_ThreadId_t replyTo = process_get_tid(pr->p);
		free(pr);
		syscall_reply(replyTo, 0);
	} else {
		pr->addr = (L4_Word_t) src;
		pager(pr);
	}
}

void copyIn(L4_ThreadId_t tid, void *src, size_t size, int append) {
	dprintf(2, "*** copyIn: tid=%ld src=%p size=%d\n",
			L4_ThreadNo(tid), src, size);

	copyInPrepare(tid, src, size, append);

	pager(newPagerRequest(
				process_lookup(L4_ThreadNo(tid)),
				(L4_Word_t) src,
				copyInContinue));
}

static void copyOutContinue(PagerRequest *pr) {
	dprintf(3, "*** copyOutContinue pr=%p process=%p tid=%ld addr=%p\n",
			pr, pr->p, process_get_tid(pr->p), (void*) pr->addr);

	// Data about the copyout operation.
	int threadNum = L4_ThreadNo(process_get_tid(pr->p));

	L4_Word_t size = LO_HALF(copyInOutData[threadNum]);
	L4_Word_t offset = HI_HALF(copyInOutData[threadNum]);

	// Continue copying out from where we left off
	char *src = pager_buffer(process_get_tid(pr->p)) + offset;
	dprintf(3, "*** copyOutContinue: size=%ld offset=%ld src=%p\n",
			size, offset, src);

	char *dest = (char*) (*pageTableLookup(
				process_get_pagetable(pr->p), pr->addr) & ADDRESS_MASK);

	dest += pr->addr & (PAGESIZE - 1);
	dprintf(3, "*** copyOutContinue: dest=%p\n", dest);

	while ((offset < size) && (offset < MAX_IO_BUF)) {
		*dest = *src;
		dest++;
		src++;
		offset++;

		if (isPageAligned(dest)) break; // continue from next page boundary
	}

	copyInOutData[threadNum] = size | (offset << 16);

	if ((offset >= size) || (offset >= MAX_IO_BUF)) {
		L4_ThreadId_t replyTo = process_get_tid(pr->p);
		free(pr);
		syscall_reply(replyTo, 0);
	} else {
		pr->addr = (L4_Word_t) dest;
		pager(pr);
	}
}

void copyOut(L4_ThreadId_t tid, void *dest, size_t size, int append) {
	dprintf(2, "*** copyOut: tid=%ld dest=%p size=%d\n",
			L4_ThreadNo(tid), dest, size);

	copyOutPrepare(tid, dest, size, append);

	pager(newPagerRequest(
				process_lookup(L4_ThreadNo(tid)),
				(L4_Word_t) dest,
				copyOutContinue));
}

