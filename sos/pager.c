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
// Pager is called from the syscall loop whenever a page fault occurs. The
// current implementation simply maps whichever pages are asked for.
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

#define verbose 2

extern void __malloc_init(uintptr_t head_base,  uintptr_t heap_end);

AddrSpace addrspace[MAX_ADDRSPACES];

static uintptr_t
page_align_up(uintptr_t adr)
{
	int pageoffset = adr % PAGESIZE;
	if (pageoffset > 0) {
		adr += (PAGESIZE - pageoffset);
	}

	return adr;
}

void
as_init()
{
	for (int i = 0; i < MAX_ADDRSPACES; i++)
	{
		addrspace[i].pagetb = NULL;
		addrspace[i].regions = NULL;
	}
}

void init_bootmem(AddrSpace *as) {
	//uintptr_t vb = PAGESIZE;

	for (Region *r = as->regions; r != NULL; r = r->next)
	{
		r->vbase = r->pbase;
		//vb += r->size;
		//vb = page_align_up(vb);
	}
}

uintptr_t
add_stackheap(AddrSpace *as) {
	uintptr_t top = (uintptr_t) (-PAGESIZE);

	for (Region *r = as->regions; r != NULL; r = r->next)
	{
		if ((r->vbase + r->size) > top)
			top = r->vbase + r->size;
	}

	top = page_align_up(top);
	Region *heap = (Region *)malloc(sizeof(Region));
	heap->size = ONE_MEG;
	heap->vbase = top;
	heap->pbase = 0;
	heap->rights = REGION_READ | REGION_WRITE;

	top = (uintptr_t) (-PAGESIZE);
	Region *stack = (Region *)malloc(sizeof(Region));
	stack->size = ONE_MEG;
	stack->vbase = top - stack->size;
	stack->pbase = 0;
	stack->rights = REGION_READ | REGION_WRITE;

	stack->next = heap;
	heap->next = as->regions;
	as->regions = stack;

	//__malloc_init(heap->vbase, heap->vbase + heap->size);

	return stack->vbase + stack->size; // stack pointer
}

uintptr_t
phys2virt(AddrSpace *as, uintptr_t phys) {
	for (Region *r = as->regions; r != NULL; r = r->next)
	{
		if (phys >= r->pbase && phys < (r->pbase + r->size))
		{
			return r->vbase + (phys - r->pbase);
		}
	}

	dprintf(0, "*** phys2virt: asked for phys address outside region!\n");
	return 0;
}

static L4_Word_t*
findPageTableWord(PageTable1 *level1, L4_Word_t addr) {
	addr /= PAGESIZE;
	int offset1 = addr / PAGETABLE_SIZE2;
	int offset2 = addr - (offset1 * PAGETABLE_SIZE2);

	dprintf(1, "*** findPageTableWord: addr=0x%lx, offset1=0x%lx, offset2=0x%lx\n",
			addr * PAGESIZE, offset1, offset2);

	if (level1 == NULL) {
		dprintf(0, "!!! findPageTableWord: level1 is NULL!\n");
		return NULL;
	} else if (level1->pages2 == NULL) {
		dprintf(0, "!!! findPageTableWord: level1->pages2 is NULL!\n");
		return NULL;
	}

	dprintf(0, "... findPageTableWord: 1\n");
	if (level1->pages2[offset1] == NULL) {
		dprintf(0, "... findPageTableWord: 2\n");
		level1->pages2[offset1] = (PageTable2*) malloc(sizeof(PageTable2));

		dprintf(0, "... findPageTableWord: 3\n");
		for (int i = 0; i < PAGETABLE_SIZE2; i++) {
			level1->pages2[offset1]->pages[i] = 0;
		}
	}

	dprintf(0, "... findPageTableWord: 4\n");
	return &level1->pages2[offset1]->pages[offset2];
}

void
pager(L4_ThreadId_t tid, L4_Msg_t *msgP)
{
	// Get the faulting address
	//L4_Word_t addr = L4_MsgWord(msgP, 0);
	L4_Word_t addr = L4_MsgWord(msgP, 0) & ~(PAGESIZE - 1);
	L4_Word_t ip = L4_MsgWord(msgP, 1);
	AddrSpace *as = &addrspace[L4_SpaceNo(L4_SenderSpace())];

	L4_Word_t frame;
	int rights;

	dprintf(1, "*** pager: fault on tid=0x%lx, ss=%d, addr=%p, ip=%p\n",
			L4_ThreadNo(tid), L4_SpaceNo(L4_SenderSpace()), addr, ip);

	if (L4_IsSpaceEqual(L4_SenderSpace(), L4_rootspace)) {
		// Root task will page fault before page table is actually
		// set up, so map 1:1.
		frame = addr;
		rights = L4_FullyAccessible;
	} else {
		// Find region it belongs in.
		Region *r;
		for (r = as->regions; r != NULL; r = r->next) {
			if (addr >= r->vbase && addr < r->vbase + r->size) break;
		}

		if (r == NULL) {
			dprintf(0, "*** pager: didn't find region!\n");
			return;
		}

		// Add entry to page table if it hasn't been already
		dprintf(0, "... pager: 1\n");
		L4_Word_t *entry = findPageTableWord(as->pagetb, addr);
		dprintf(0, "... pager: 2\n");
		if (*entry == 0) {
			*entry = frame_alloc();
		}

		dprintf(0, "... pager: 3\n");
		frame = *entry;
		dprintf(0, "... pager: 4\n");
		rights = r->rights;
	}

	// Map physical to virtual memory
	dprintf(0, "... pager: 5\n");
	L4_Fpage_t targetFpage = L4_FpageLog2(addr, 12);
	//L4_Set_Rights(&targetFpage, r->rights);
	dprintf(0, "... pager: 6\n");
	L4_Set_Rights(&targetFpage, rights);
	//L4_PhysDesc_t phys = L4_PhysDesc(*entry, L4_DefaultMemory);
	dprintf(0, "... pager: 7\n");
	L4_PhysDesc_t phys = L4_PhysDesc(frame, L4_DefaultMemory);

	dprintf(0, "... pager: 8\n");
	if (!L4_MapFpage(L4_SenderSpace(), targetFpage, phys) ) {
		sos_print_error(L4_ErrorCode());
		printf(" Can't map page at %lx for tid %lx, ip = %lx\n", addr, tid.raw, ip);
	}
}

