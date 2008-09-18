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
#include "process.h"
#include "thread.h"

#define verbose 1

// Page table structures
typedef struct PageTable2_t {
	L4_Word_t pages[PAGETABLE_SIZE2];
} PageTable2;

typedef struct PageTable1_t {
	PageTable2 *pages2[PAGETABLE_SIZE1];
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

// The pager process
#define PAGER_STACK_SIZE (4 * (PAGESIZE))

L4_Word_t sos_pager_stack[PAGER_STACK_SIZE];
L4_ThreadId_t sos_pager; // automatically initialised to 0 (L4_nilthread)
static void pager_handler(void);

// XXX
//
// typedef struct {
// 	void *ptr;
// 	rights r;
// } userptr_t;

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
	PageTable1 *pt = (PageTable1*) malloc(sizeof(PageTable1));

	for (int i = 0; i < PAGETABLE_SIZE1; i++) {
		pt->pages2[i] = NULL;
	}

	return (PageTable*) pt;
}

void
pager_init(void) {
	// Start the real pager process
	Process *pager = process_init();

	process_prepare(pager);
	process_set_ip(pager, (void*) pager_handler);
	process_set_sp(pager, sos_pager_stack + PAGER_STACK_SIZE - 1);

	// XXX race condition with other processes?
	// May want to use IPC synchronisation with SOS
	process_run(pager);
	sos_pager = process_get_tid(pager);
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

static void
doPager(L4_Word_t addr, L4_Word_t ip) {
	Process *p = process_lookup(L4_SpaceNo(L4_SenderSpace()));
	L4_Word_t frame;
	int rights;
	int mapKernelToo = 0;

	dprintf(1, "*** pager: fault on ss=%d, addr=%p (%p), ip=%p\n",
			L4_SpaceNo(L4_SenderSpace()), addr, addr & PAGEALIGN, ip);

	addr &= PAGEALIGN;

	if (L4_IsSpaceEqual(L4_SenderSpace(), L4_rootspace)) {
		// Root task will page fault before page table is actually
		// set up, so map 1:1.
		frame = addr;
		rights = L4_FullyAccessible;
	} else if (L4_SpaceNo(L4_SenderSpace()) == L4_ThreadNo(sos_pager)) {
		// Hack pager thread for now - eventually the roottask will do
		// this automatically, and everything below will actually be
		// handled in the pager process
		frame = addr;
		rights = L4_FullyAccessible;
	} else {
		// Find region it belongs in.
		Region *r = findRegion(process_get_regions(p), addr);

		if (r == NULL) {
			printf("Segmentation fault\n");
			thread_kill(sos_sid2tid(L4_SenderSpace())); // XXX kill thread not addrspace
			return;
		}

		// Place in, or retrieve from, page table.
		L4_Word_t *entry = findPageTableWord(process_get_pagetable(p), addr);

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
	dprintf(2, "*** pager: about to invoke doPager\n");
	doPager(L4_MsgWord(msgP, 0), L4_MsgWord(msgP, 1));
	dprintf(2, "*** pager: successfully invoked doPager\n");
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
	Process *p = process_lookup(L4_SpaceNo(L4_SenderSpace()));

	// Check that addr is in valid region
	Region *r = findRegion(process_get_regions(p), addr);
	if (r == NULL) {
		thread_kill(sos_sid2tid(L4_SenderSpace())); // XXX kill thread not addrspace
		return NULL;
	}

	// Find equivalent physical address - the address might
	// not actually be in the page table yet, so may need to
	// invoke pager manually.
	L4_Word_t physAddr;
	while ((physAddr = *findPageTableWord(process_get_pagetable(p), addr & PAGEALIGN)) == 0) {
		dprintf(1, "*** sender2kernel: %p wasn't mapped in, doing manually\n", addr);
		doPager(addr, (L4_Word_t) -1);
	}

	physAddr += (L4_Word_t) (addr & (PAGESIZE - 1));
	return (L4_Word_t*) physAddr;
}

static void pager_handler(void) {
	printf("pager handler started\n");
	
	// syscall loop
	for (;;) {
		;
	}
}

