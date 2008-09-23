/* 
 * sos/swapfile.c
 *
 * Simple swap file implementation.
 *
 * Hands out free PAGESIZE slots to write out to in the swap file in O(1) time.
 * Currently  only use one frame of memory for the swap file structure so
 * only supports a swap file size of (PAGESIZE / sizeof(L4_Word_t)). On the
 * Arm5 processor this is 4MB.
 */

#include "swapfile.h"

#include "frames.h"
#include "l4.h"
#include "libsos.h"
#include "pager.h"

#define verbose 2

#define SWAPSIZE (PAGESIZE / sizeof(L4_Word_t))
#define NULL_SLOT ((L4_Word_t) (-1))

static L4_Word_t *FileSlots;
static L4_Word_t NextSlot;
static L4_Word_t LastSlot;
static L4_Word_t SlotsFree;

void
swapfile_init(void)
{
	dprintf(1, "*** swapfile_init: Initialising swapfile ***\n");
	FileSlots = (L4_Word_t *) kframe_alloc();
	NextSlot = 0;
	LastSlot = 0;
	SlotsFree = SWAPSIZE;

	for (L4_Word_t i = 0, j = 0; i < SWAPSIZE; i++, j+= PAGESIZE)
	{
		FileSlots[i] = j;
	}
}

L4_Word_t
get_swapslot(void)
{
	if (SlotsFree <= 0)
	{
		return NULL_SLOT;
	}

	L4_Word_t r = FileSlots[NextSlot];
	NextSlot = ((NextSlot + 1) % SWAPSIZE);
	SlotsFree--;

	return r;
}

int
free_swapslot(L4_Word_t slot)
{
	if (SlotsFree == SWAPSIZE || slot > PAGESIZE * (SWAPSIZE - 1))
	{
		return -1;
	}

	FileSlots[LastSlot] = slot;
	LastSlot = ((LastSlot + 1) % SWAPSIZE);
	SlotsFree++;

	return 0;
}

