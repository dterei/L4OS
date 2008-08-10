/****************************************************************************
 *
 *      $Id: frames.c,v 1.3 2003/08/06 22:52:04 benjl Exp $
 *
 *      Description: Example frame table implementation
 *
 *      Author:      Ben Leslie
 *
 ****************************************************************************/

#include <stddef.h>

#include <l4/space.h>
#include <l4/types.h>

#include "libsos.h"

#include "frames.h"

#define NULLFRAME ((L4_Word_t) (0))
#define verbose 1

static L4_Word_t firstFree;
static L4_Word_t lastFree;

/*
  Initialise the frame table. The current implementation is
  clearly not sufficient.
*/
void
frame_init(L4_Word_t low, L4_Word_t frame)
{
	static L4_Word_t page;
	static L4_Word_t high;
	static L4_Fpage_t fpage;
	static L4_PhysDesc_t ppage;

	// Make the high address page aligned (grr).
	high = frame + 1;

	// Hungry!
	dprintf(0, "Trying to map pages.\n");
	for (page = low; page < high; page += PAGESIZE) {
		fpage = L4_Fpage(page, PAGESIZE);
		L4_Set_Rights(&fpage, L4_ReadWriteOnly);
		ppage = L4_PhysDesc(page, 0);
		L4_MapFpage(L4_rootspace, fpage, ppage);
	}

	// Make everything free.
	dprintf(0, "Trying to initialise linked list.\n");
	for (page = low; page < high - PAGESIZE; page += PAGESIZE) {
		*((L4_Word_t*) page) = page + PAGESIZE;
	}

	dprintf(0, "Trying to set bounds of linked list.\n");
	firstFree = low;
	lastFree = high - PAGESIZE;
	*((L4_Word_t*) lastFree) = NULLFRAME;
}

/*
  Allocate a currently unused frame 
*/
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

/*
  Add a frame to the free frame list
*/
void
frame_free(L4_Word_t frame)
{
	*((L4_Word_t*) frame) = NULLFRAME;

	if (firstFree == NULLFRAME) {
		// No free memory, so need to redo both pointers.
		firstFree = frame;
		lastFree = frame;
	} else {
		*((L4_Word_t*) lastFree) = frame;
		lastFree = frame;
	}
}
