#include <stddef.h>

#include "l4.h"
#include "libsos.h"
#include "pager.h"
#include "frames.h"

#define verbose 1

#define NULLFRAME ((L4_Word_t) (0))

static L4_Word_t firstFree;

void
frame_init(L4_Word_t low, L4_Word_t frame)
{
	L4_Word_t page, high;
	L4_Fpage_t fpage;
	L4_PhysDesc_t ppage;

	// Make the high address page aligned (grr).
	high = frame + 1;

	// Hungry!
	dprintf(1, "*** frame_init: trying to map from %p to %p\n", low, high);
	for (page = low; page < high; page += PAGESIZE) {
		fpage = L4_Fpage(page, PAGESIZE);
		L4_Set_Rights(&fpage, L4_ReadWriteOnly);
		ppage = L4_PhysDesc(page, L4_DefaultMemory);
		L4_MapFpage(L4_rootspace, fpage, ppage);
	}

	// Make everything free.
	dprintf(1, "*** frame_init: trying to initialise linked list.\n");
	for (page = low; page < high - PAGESIZE; page += PAGESIZE) {
		*((L4_Word_t*) page) = page + PAGESIZE;
	}

	dprintf(1, "*** frame_init: trying to set bounds of linked list.\n");
	firstFree = low;
	*((L4_Word_t*) (high - PAGESIZE)) = NULLFRAME;
}

L4_Word_t
frame_alloc(void)
{
	static L4_Word_t alloc;

	alloc = firstFree;

	if (alloc != NULLFRAME) {
		firstFree = *((L4_Word_t*) firstFree);
	} else {
		// There is no free memory.
	}

	return alloc;
}

void
frame_free(L4_Word_t frame)
{
	*((L4_Word_t*) frame) = firstFree;
	firstFree = frame;
}

