#include <stddef.h>

#include "cache.h"
#include "constants.h"
#include "libsos.h"
#include "frames.h"
#include "l4.h"

#define verbose 1

#define NULLFRAME ((L4_Word_t) (0))

static int totalFrames;
static L4_Word_t firstFree;
static int totalInUse;

void frame_init(L4_Word_t low, L4_Word_t frame) {
	L4_Word_t page, high;
	L4_Fpage_t fpage;
	L4_PhysDesc_t ppage;
	totalFrames = 0;

	// Make the high address page aligned (grr).
	high = frame + 1;

	// Hungry!
	dprintf(1, "*** frame_init: trying to map from %p to %p\n", low, high);
	for (page = low; page < high; page += PAGESIZE) {
		fpage = L4_Fpage(page, PAGESIZE);
		L4_Set_Rights(&fpage, L4_ReadWriteOnly);
		ppage = L4_PhysDesc(page, DEFAULT_MEMORY);
		L4_MapFpage(L4_rootspace, fpage, ppage);
	}

	// Make everything free.
	dprintf(1, "*** frame_init: trying to initialise linked list.\n");
	for (page = low; page < high - PAGESIZE; page += PAGESIZE) {
		*((L4_Word_t*) page) = page + PAGESIZE;
		totalFrames++;
	}

	dprintf(1, "*** frame_init: trying to set bounds of linked list.\n");
	firstFree = low;
	*((L4_Word_t*) (high - PAGESIZE)) = NULLFRAME;

	totalInUse = 0;
}

L4_Word_t frame_alloc(alloc_codes_t reason) {
	static L4_Word_t alloc;

	alloc = firstFree;

	if (alloc != NULLFRAME) {
		firstFree = *((L4_Word_t*) firstFree);
		totalInUse++;
	} else {
		// There is no free memory.
		dprintf(0, "!!! frame_alloc: NO MEMORY\n");
	}

	dprintf(2, "frames: allocated frame for %d: %p\n", reason, alloc);
	return alloc;
}

void frame_free(L4_Word_t frame) {
	*((L4_Word_t*) frame) = firstFree;
	firstFree = frame;
	totalInUse--;
	dprintf(2, "frames: free'd frame: %p\n", frame);
}

int frames_allocated(void) {
	return totalInUse;
}

int frames_free(void) {
	return totalFrames - totalInUse;
}

int frames_total(void) {
	return totalFrames;
}

