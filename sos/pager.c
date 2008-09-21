/****************************************************************************
 *
 *      $Id: pager.c,v 1.4 2003/08/06 22:52:04 benjl Exp $
 *
 *      Description: Example pager for the SOS project.
 *
 *      Author:			Godfrey van der Linden
 *      Original Author:	Ben Leslie
 *
 ****************************************************************************/


//
// Pager is called from the syscall loop whenever a page fault occurs.//
//

#include <sos/sos.h>
#include <stdio.h>
#include <stdlib.h>

#include "frames.h"
#include "l4.h"
#include "libsos.h"
#include "pager.h"
#include "process.h"
#include "thread.h"
#include "syscall.h"

#define verbose 1

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

// Asynchronous pager requests
typedef struct PagerRequest_t PagerRequest;
struct PagerRequest_t {
	Process *p;
	L4_Word_t addr;
	void (*callback)(PagerRequest *pr);
};

static PagerRequest **requests = NULL;

// For copyin/copyout
//static L4_Word_t *copyInOutPointers;
static L4_Word_t *copyInOutData;
static char *copyInOutBuffer;

// The pager process
#define PAGER_STACK_SIZE PAGESIZE

static L4_Word_t virtualPagerStack[PAGER_STACK_SIZE];
L4_ThreadId_t virtual_pager; // automatically initialised to 0 (L4_nilthread)
static void virtualPagerHandler(void);

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

PageTable *pagetable_init(void) {
	assert(sizeof(PageTable1) == PAGESIZE);
	PageTable1 *pt = (PageTable1*) frame_alloc();

	for (int i = 0; i < PAGEWORDS; i++) {
		pt->pages2[i] = NULL;
	}

	return (PageTable*) pt;
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

void
pager_init(void) {
	// The array of page table requests (per thread id)
	requests = (PagerRequest**) allocFrames(sizeof(PagerRequest*));

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
	virtual_pager = process_get_tid(pager);
}

int
sos_moremem(uintptr_t *base, unsigned int nb) {
	dprintf(1, "*** sos_moremem(%p, %lx)\n", base, nb);

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
	dprintf(1, "*** sos_moremem: was %p %lx\n", heap->base, heap->size);
	heap->size += nb;
	dprintf(1, "*** sos_moremem: now %p %lx\n", heap->base, heap->size);

	// Have the option of returning 0 to signify no more memory.
	return 1;
}

static L4_Word_t*
findPageTableWord(PageTable *pt, L4_Word_t addr) {
	PageTable1 *level1 = (PageTable1*) pt;

	addr /= PAGESIZE;
	int offset1 = addr / PAGEWORDS;
	int offset2 = addr - (offset1 * PAGEWORDS);

	if (level1 == NULL) {
		dprintf(0, "!!! findPageTableWord: level1 is NULL!\n");
		return NULL;
	} else if (level1->pages2 == NULL) {
		dprintf(0, "!!! findPageTableWord: level1->pages2 is NULL!\n");
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
	newPr->callback = callback;

	return newPr;
}

static void pagerContinue(PagerRequest *pr) {
	dprintf(1, "*** pagerContinue: replying to %ld\n",
			L4_ThreadNo(process_get_tid(pr->p)));

	L4_ThreadId_t replyTo = process_get_tid(pr->p);
	free(pr);
	syscall_reply(replyTo, 0);
}

static void
pager(PagerRequest *pr) {
	L4_Word_t frame;
	int rights;
	int mapKernelToo = 0;
	L4_Word_t addr = pr->addr & PAGEALIGN;

	dprintf(1, "*** pager: fault on ss=%d, addr=%p (%p)\n",
			L4_SpaceNo(L4_SenderSpace()), pr->addr, addr);
	assert(!L4_IsSpaceEqual(L4_SenderSpace(), L4_rootspace));

	// Find region it belongs in.
	dprintf(1, "*** pager: finding region\n");
	Region *r = findRegion(process_get_regions(pr->p), addr);
	dprintf(1, "*** pager: found region %p\n", r);

	if (r == NULL) {
		printf("Segmentation fault\n");
		thread_kill(process_get_tid(pr->p));
		return;
	}

	// Place in, or retrieve from, page table.
	dprintf(1, "*** pager: finding entry\n");
	L4_Word_t *entry = findPageTableWord(process_get_pagetable(pr->p), addr);

	dprintf(1, "*** pager: entry found to be %p at %p\n", (void*) *entry, entry);

	if (*entry != 0) {
		// Already appears in page table, just got unmapped somehow.
		dprintf(1, "*** pager: already mapped to %p\n", (void*) *entry);
	} else if (r->mapDirectly) {
		// Wants to be mapped directly (code/data probably).
		// In this case the kernel doesn't know about it from the
		// frame table, so map it 1:1 in the kernel too.
		dprintf(1, "*** pager: mapping directly\n");
		*entry = addr;
		mapKernelToo = 1;
	} else {
		dprintf(1, "*** pager: allocating frame\n");
		*entry = frame_alloc();
	}

	frame = *entry;
	rights = r->rights;

	L4_SpaceId_t sid = sos_tid2sid(process_get_tid(pr->p));
	L4_Fpage_t fpage = L4_Fpage(addr, PAGESIZE);
	L4_Set_Rights(&fpage, rights);
	L4_PhysDesc_t ppage = L4_PhysDesc(frame, L4_DefaultMemory);

	dprintf(1, "*** pager: mapping %lx with sid=%lx frame=%ld rights=%d\n",
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

	dprintf(1, "*** pager: finished, waking faulter\n");
	pr->callback(pr);
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

		dprintf(1, "*** virtualPagerHandler: got request from %ld\n",
				L4_ThreadNo(process_get_tid(p)));

		switch (TAG_SYSLAB(tag)) {
			case L4_PAGEFAULT:
				pr = newPagerRequest(p, L4_MsgWord(&msg, 0), pagerContinue);
				pager(pr);
				break;

			default:
				dprintf(0, "!!! virtualPagerHandler: unhandled message!\n");
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
	dprintf(1, "*** copyInContinue pr=%p process=%p tid=%ld addr=%p\n",
			pr, pr->p, process_get_tid(pr->p), (void*) pr->addr);

	// Data about the copyin operation.
	int threadNum = L4_ThreadNo(process_get_tid(pr->p));

	L4_Word_t size = copyInOutData[threadNum] & 0x0000ffff;
	L4_Word_t offset = (copyInOutData[threadNum] >> 16) & 0x0000ffff;

	// Continue copying in from where we left off
	char *dest = pager_buffer(process_get_tid(pr->p)) + offset;
	dprintf(1, "*** copyInContinue: size=%ld offset=%ld dest=%p\n",
			size, offset, dest);

	// Assume it's already there - this function should only get
	// called as a continutation from the pager, so no problem
	char *src = (char*) *findPageTableWord(
			process_get_pagetable(pr->p), pr->addr);
	src += pr->addr & (PAGESIZE - 1);
	dprintf(1, "*** copyInContinue: src=%p\n", src);

	// Start the copy - if we reach a page boundary then pause computation
	// since we can assume that it goes out to disk and waits a long time
	do {
		*dest = *src;
		dest++;
		src++;
		offset++;
	} while ((offset < size) && ((((L4_Word_t) src) & (PAGESIZE - 1)) != 0));

	// Reached end - either we finished the copy, or we reached a boundary
	if (offset >= size) {
		L4_ThreadId_t replyTo = process_get_tid(pr->p);
		free(pr);
		syscall_reply(replyTo, 0);
	} else {
		copyInOutData[threadNum] = size | (offset << 16);
		pr->addr = (L4_Word_t) src;
		pager(pr);
	}
}

void copyIn(L4_ThreadId_t tid, void *src, size_t size, int append) {
	dprintf(1, "*** copyIn: tid=%ld src=%p size=%d\n",
			L4_ThreadNo(tid), src, size);

	if (append) {
		copyInOutData[L4_ThreadNo(tid)] &= 0x0000ffff;
		copyInOutData[L4_ThreadNo(tid)] |= size;
	} else {
		copyInOutData[L4_ThreadNo(tid)] = size;
	}

	pager(newPagerRequest(
				process_lookup(L4_ThreadNo(tid)),
				(L4_Word_t) src,
				copyInContinue));
}

static void copyOutContinue(PagerRequest *pr) {
	dprintf(1, "*** copyOutContinue pr=%p process=%p tid=%ld addr=%p\n",
			pr, pr->p, process_get_tid(pr->p), (void*) pr->addr);

	// Data about the copyout operation.
	int threadNum = L4_ThreadNo(process_get_tid(pr->p));

	L4_Word_t size = copyInOutData[threadNum] & 0x0000ffff;
	L4_Word_t offset = (copyInOutData[threadNum] >> 16) & 0x0000ffff;

	// Continue copying out from where we left off
	char *src = pager_buffer(process_get_tid(pr->p)) + offset;
	dprintf(1, "*** copyOutContinue: size=%ld offset=%ld src=%p\n",
			size, offset, src);

	char *dest = (char*) *findPageTableWord(
			process_get_pagetable(pr->p), pr->addr);
	dest += pr->addr & (PAGESIZE - 1);
	dprintf(1, "*** copyOutContinue: dest=%p\n", dest);

	do {
		*dest = *src;
		dest++;
		src++;
		offset++;
	} while ((offset < size) && ((((L4_Word_t) dest) & (PAGESIZE - 1)) != 0));

	if (offset >= size) {
		L4_ThreadId_t replyTo = process_get_tid(pr->p);
		free(pr);
		syscall_reply(replyTo, 0);
	} else {
		copyInOutData[threadNum] = size | (offset << 16);
		pr->addr = (L4_Word_t) dest;
		pager(pr);
	}
}

void copyOut(L4_ThreadId_t tid, void *dest, size_t size, int append) {
	dprintf(1, "*** copyOut: tid=%ld dest=%p size=%d\n",
			L4_ThreadNo(tid), dest, size);

	if (append) {
		copyInOutData[L4_ThreadNo(tid)] &= 0x0000ffff;
		copyInOutData[L4_ThreadNo(tid)] |= size;
	} else {
		copyInOutData[L4_ThreadNo(tid)] = size;
	}

	pager(newPagerRequest(
				process_lookup(L4_ThreadNo(tid)),
				(L4_Word_t) dest,
				copyOutContinue));
}

