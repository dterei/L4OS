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

#include <stdio.h>
#include <stdlib.h>

#include <l4/types.h>
#include <l4/map.h>
#include <l4/misc.h>
#include <l4/space.h>
#include <l4/thread.h>

#include "frames.h"
#include "pager.h"
#include "libsos.h"

#define verbose 1

AddrSpace addrspace[MAX_ADDRSPACES];

static uintptr_t
page_align_up(uintptr_t adr)
{
	//TODO: Can be simplified, just align usually then add a page size.
	int pageoffset = adr % PAGESIZE;
	if (pageoffset > 0) {
		adr += (PAGESIZE - pageoffset);
	}

	return adr;
}

void
as_init(void) {
	for (int i = 0; i < MAX_ADDRSPACES; i++) {
		addrspace[i].pagetb = NULL;
		addrspace[i].regions = NULL;
	}
}

uintptr_t
add_stackheap(AddrSpace *as) {
	uintptr_t top = 0;

	// Find the highest address from bootinfo and put the
	// heap above it.
	for (Region *r = as->regions; r != NULL; r = r->next)
	{
		if ((r->base + r->size) > top)
			top = r->base + r->size;
	}

	// TODO: Need to actually change malloc to use this new heap area
	top = page_align_up(top);
	Region *heap = (Region *)malloc(sizeof(Region));
	heap->size = ONE_MEG;
	heap->base = top;
	heap->rights = REGION_READ | REGION_WRITE;

	// Put the stack above the heap... for no particular
	// reason.
	// TODO: Place stack at top of address space
	top = page_align_up(top + heap->size);
	Region *stack = (Region *)malloc(sizeof(Region));
	stack->size = ONE_MEG;
	stack->base = top;
	stack->rights = REGION_READ | REGION_WRITE;

	// Add them to the region list.
	stack->next = heap;
	heap->next = as->regions;
	as->regions = stack;

	// Stack pointer - the (-PAGESIZE) thing makes it work.
	// TODO: fix this hack.
	return stack->base + stack->size - PAGESIZE;
}

static L4_Word_t*
findPageTableWord(PageTable1 *level1, L4_Word_t addr) {
	addr /= PAGESIZE;
	int offset1 = addr / PAGETABLE_SIZE2;
	int offset2 = addr - (offset1 * PAGETABLE_SIZE2);

	if (level1 == NULL) {
		dprintf(0, "!!! findPageTableWord: level1 is NULL!\n");
		return NULL;
	} else if (level1->pages2 == NULL) {
		dprintf(0, "!!! findPageTableWord: level1->pages2 is NULL!\n");
		return NULL;
	}

	if (level1->pages2[offset1] == NULL) {
		level1->pages2[offset1] = (PageTable2*) malloc(sizeof(PageTable2));

		for (int i = 0; i < PAGETABLE_SIZE2; i++) {
			level1->pages2[offset1]->pages[i] = 0;
		}
	}

	return &level1->pages2[offset1]->pages[offset2];
}

void
pager(L4_ThreadId_t tid, L4_Msg_t *msgP)
{
	// Get the faulting address
	L4_Word_t addr = L4_MsgWord(msgP, 0);
	L4_Word_t ip = L4_MsgWord(msgP, 1);
	AddrSpace *as = &addrspace[L4_SpaceNo(L4_SenderSpace())];

	L4_Word_t frame;
	int rights;

	dprintf(1, "*** pager: fault on tid=0x%lx, ss=%d, addr=%p, ip=%p\n",
			L4_ThreadNo(tid), L4_SpaceNo(L4_SenderSpace()), addr, ip);

	addr &= PAGEALIGN;

	if (L4_IsSpaceEqual(L4_SenderSpace(), L4_rootspace)) {
		// Root task will page fault before page table is actually
		// set up, so map 1:1.
		frame = addr;
		rights = L4_FullyAccessible;
	} else {
		// Find region it belongs in.
		Region *r;
		for (r = as->regions; r != NULL; r = r->next) {
			if (addr >= r->base && addr < r->base + r->size) break;
		}

		if (r == NULL) {
			dprintf(0, "Segmentation fault\n");
			// TODO kill the thread (L4_ThreadControl)
		}

		if (r->mapDirectly) {
			dprintf(1, "*** pager: mapping %lx (region %p) directly\n", addr, r);
			frame = addr;
		} else {
			L4_Word_t *entry = findPageTableWord(as->pagetb, addr);
			if (*entry == 0) *entry = frame_alloc();
			frame = *entry;
		}

		rights = r->rights;
	}

	L4_Fpage_t fpage = L4_Fpage(addr, PAGESIZE);
	L4_Set_Rights(&fpage, rights);
	L4_PhysDesc_t ppage = L4_PhysDesc(frame, L4_UncachedMemory);

	if (!L4_MapFpage(L4_SenderSpace(), fpage, ppage)) {
		sos_print_error(L4_ErrorCode());
		printf("Can't map page at %lx to frame %lx for tid %lx, ip = %lx\n",
				addr, frame, tid.raw, ip);
	}

}

void
pager_flush(L4_ThreadId_t tid, L4_Msg_t *msgP)
{
	//TODO: Actually flush as. Done by mapping each page to a null frame? 0? Max Ram?

	// get null frame to map pages to
	L4_Word_t nullAddr = { 0UL };
	nullAddr &= PAGEALIGN;
	L4_PhysDesc_t nullFrame = L4_PhysDesc(nullAddr, L4_UncachedMemory);

	// map all pages to null frame
	for (L4_Word_t paddr = 0UL; paddr < (L4_Word_t) (-1); paddr += PAGESIZE) {
		paddr &= PAGEALIGN;

		L4_Fpage_t page = L4_Fpage(paddr, PAGESIZE);
		L4_Set_Rights(&page, L4_NoAccess);

		if (!L4_MapFpage(L4_SenderSpace(), page, nullFrame)) {
			sos_print_error(L4_ErrorCode());
			printf("Can't map page at %lx to %lx for tid %lx\n",
					paddr, nullAddr, tid.raw);
		}

	}

}

