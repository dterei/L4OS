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

static int allocCodes[FA_PAGERALLOC + 1];

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

	// And for debugging purposes, keep track of the alloc codes
	for (int i = 0; i <= FA_PAGERALLOC; i++) {
		allocCodes[i] = 0;
	}
}

L4_Word_t frame_alloc(alloc_codes_t reason) {
	static L4_Word_t alloc;

	alloc = firstFree;
	assert(alloc != NULLFRAME);

	firstFree = *((L4_Word_t*) firstFree);

	dprintf(2, "frames: allocated frame for %d: %p\n", reason, alloc);
	totalInUse++;
	allocCodes[reason]++;
	return alloc;
}

void frames_print_allocation(void) {
	printf("FA_STACK:       %4d\n", allocCodes[FA_STACK]);
	printf("FA_SWAPFILE:    %4d\n", allocCodes[FA_SWAPFILE]);
	printf("FA_MORECORE:    %4d\n", allocCodes[FA_MORECORE]);
	printf("FA_SWAPPIN:     %4d\n", allocCodes[FA_SWAPPIN]);
	printf("FA_MMAP_READ:   %4d\n", allocCodes[FA_MMAP_READ]);
	printf("FA_ELFLOAD:     %4d\n", allocCodes[FA_ELFLOAD]);
	printf("FA_PAGETABLE1:  %4d\n", allocCodes[FA_PAGETABLE1]);
	printf("FA_PAGETABLE2:  %4d\n", allocCodes[FA_PAGETABLE2]);
	printf("FA_ALLOCFRAMES: %4d\n", allocCodes[FA_ALLOCFRAMES]);
	printf("FA_PAGERALLOC:  %4d\n", allocCodes[FA_PAGERALLOC]);
}

void frame_free(L4_Word_t frame) {
	assert(frame != NULLFRAME);
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

