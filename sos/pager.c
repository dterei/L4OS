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

#include "frames.h"
#include "l4.h"
#include "libsos.h"
#include "pager.h"
#include "thread.h"

#define verbose 1

// XXX
//
// typedef struct {
// 	void *ptr;
// 	rights r;
// } userptr_t;

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

	top = page_align_up(top);

	// Size of the region is zero for now, expand as we get
	// syscalls (more_heap).
	Region *heap = (Region *)malloc(sizeof(Region));
	heap->type = REGION_HEAP;
	heap->size = 0;
	heap->base = top;
	heap->rights = REGION_READ | REGION_WRITE;

	// Put the stack half way up the address space - at the top
	// causes pagefaults, so halfway up seems like a nice compromise.
	top = page_align_up(((unsigned int) -1) >> 1);

	Region *stack = (Region *)malloc(sizeof(Region));
	stack->type = REGION_STACK;
	stack->size = ONE_MEG;
	stack->base = top - stack->size;
	stack->rights = REGION_READ | REGION_WRITE;

	// Add them to the region list.
	stack->next = heap;
	heap->next = as->regions;
	as->regions = stack;

	// crt0 (whatever that is) will pop 3 words off stack,
	// hence the need to subtact 3 words from the sp.
	return stack->base + stack->size - (3 * sizeof(L4_Word_t));
}

int
sos_moremem(uintptr_t *base, uintptr_t *top, unsigned int nb) {
	dprintf(0, "*** sos_moremem(%p, %p, %lx)\n", base, top, nb);

	// Find the current heap section.
	AddrSpace *as = &addrspace[L4_SpaceNo(L4_SenderSpace())];
	Region *heap;

	for (heap = as->regions; heap != NULL; heap = heap->next) {
		if (heap->type == REGION_HEAP) {
			break;
		}
	}

	if (heap == NULL) {
		// No heap!?
		dprintf(0, "!!! sos_more_heap: no heap region found!\n");
		return 0;
	}

	// Move the heap region manually
	dprintf(0, "*** sos_moremem: was %p %lx\n", heap->base, heap->size);
	nb = page_align_up(nb);
	heap->size += nb / sizeof(L4_Word_t);
	dprintf(0, "*** sos_moremem: now %p %lx\n", heap->base, heap->size);

	*base = heap->base;
	*top = heap->base + nb;

	// Have the option of returning 0 to signify no more memory
	return 1;
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

static Region*
findRegion(AddrSpace *as, L4_Word_t addr) {
	Region *r;

	for (r = as->regions; r != NULL; r = r->next) {
		if (addr >= r->base && addr < r->base + r->size) break;
	}

	return r;
}

static void
doPager(L4_Word_t addr, L4_Word_t ip) {
	int sid = L4_SpaceNo(L4_SenderSpace());
	AddrSpace *as = &addrspace[sid];
	L4_Word_t frame;
	int rights;
	int mapKernelToo = 0;

	dprintf(1, "*** pager: fault on ss=%d, addr=%p, ip=%p\n",
			L4_SpaceNo(L4_SenderSpace()), addr, ip);

	addr &= PAGEALIGN;

	if (L4_IsSpaceEqual(L4_SenderSpace(), L4_rootspace)) {
		// Root task will page fault before page table is actually
		// set up, so map 1:1.
		frame = addr;
		rights = L4_FullyAccessible;
	} else {
		// Find region it belongs in.
		Region *r = findRegion(as, addr);

		if (r == NULL) {
			printf("Segmentation fault\n");
			thread_kill(L4_GlobalId(sid, 1));
			return;
		}

		// Place in, or retrieve from, page table.
		L4_Word_t *entry = findPageTableWord(as->pagetb, addr);

		if (*entry != 0) {
			// Already appears in page table, just got unmapped somehow.
		} else if (r->mapDirectly) {
			// Wants to be mapped directly (code/data probably).
			// In this case the kernel doesn't know about it from the
			// frame table, so map it 1:1 in the kernel too.
			*entry = addr;
			mapKernelToo = 1;
		} else {
			*entry = frame_alloc();
		}

		frame = *entry;
		rights = r->rights;
	}

	L4_Fpage_t fpage = L4_Fpage(addr, PAGESIZE);
	L4_Set_Rights(&fpage, rights);
	L4_PhysDesc_t ppage = L4_PhysDesc(frame, L4_DefaultMemory);

	if (!L4_MapFpage(L4_SenderSpace(), fpage, ppage)) {
		sos_print_error(L4_ErrorCode());
		dprintf(0, "Can't map page at %lx to frame %lx for ip = %lx\n",
				addr, frame, ip);
	}

	if (mapKernelToo) {
		if (!L4_MapFpage(L4_rootspace, fpage, ppage)) {
			sos_print_error(L4_ErrorCode());
			dprintf(0, "Failed mapping to kernel too\n");
		}
	}
}

void
pager(L4_ThreadId_t tid, L4_Msg_t *msgP) {
	doPager(L4_MsgWord(msgP, 0), L4_MsgWord(msgP, 1));
}

void
pager_flush(L4_ThreadId_t tid, L4_Msg_t *msgP)
{
	// There is actually a magic fpage that we can use to unmap
	// the whole address space - and I assume we're meant to
	// unmap it from the sender space.
	if (!L4_UnmapFpage(L4_SenderSpace(), L4_CompleteAddressSpace)) {
		sos_print_error(L4_ErrorCode());
		printf("!!! pager_flush: failed to unmap complete address space\n");
	}
}

L4_Word_t*
sender2kernel(L4_Word_t addr) {
	dprintf(1, "*** sender2kernel: addr=%p\n", addr);
	int sid = L4_SpaceNo(L4_SenderSpace());
	AddrSpace *as = &addrspace[sid];

	// Check that addr is in valid region
	Region *r = findRegion(as, addr);
	if (r == NULL) {
		thread_kill(L4_GlobalId(sid, 1));
		return NULL;
	}

	// Find equivalent physical address - the address might
	// not actually be in the page table yet, so may need to
	// invoke pager manually.
	L4_Word_t physAddr;
	while ((physAddr = *findPageTableWord(as->pagetb, addr & PAGEALIGN)) == 0) {
		dprintf(1, "*** sender2kernel: %p wasn't mapped in, doing manually\n", addr);
		doPager(addr, (L4_Word_t) -1);
	}

	physAddr += (L4_Word_t) (addr & (PAGESIZE - 1));
	return (L4_Word_t*) physAddr;
}

