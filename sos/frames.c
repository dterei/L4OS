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

#include "l4.h"
#include "libsos.h"
#include "pager.h"
#include "frames.h"

#define verbose 1

#define FRAME_ALLOC_LIMIT 16
#define NULLFRAME ((L4_Word_t) (0))
// XXX add to a global collection of frame/pagetable flags,
// so they don't overlap.
#define REFBIT_MASK 0x00000001

static L4_Word_t firstFree;

typedef struct FrameList_t FrameList;
struct FrameList_t {
	L4_Word_t frame; // also contains refbit
	FrameList *next;
};

static int allocLimit;
static FrameList *allocHead;
static FrameList *allocLast;

void
frame_init(L4_Word_t low, L4_Word_t frame)
{
	L4_Word_t page, high;
	L4_Fpage_t fpage;
	L4_PhysDesc_t ppage;

	// Set up alloc frame list
	allocLimit = FRAME_ALLOC_LIMIT;
	allocHead = NULL;
	allocLast = NULL;

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

static L4_Word_t
doFrameAlloc(void)
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

static void
addFrameList(L4_Word_t frame) {
	FrameList *new = (FrameList*) malloc(sizeof(FrameList));
	new->frame = frame;

	// The frame should definitely not have been referenced
	assert((new->frame & REFBIT_MASK) == 0);

	if (allocLast == NULL) {
		allocLast = new->next;
		assert(allocHead == NULL);
		allocHead = new->next;
	} else {
		new->next = NULL;
		allocLast->next = new;
	}
}

L4_Word_t
frame_alloc(void) {
	L4_Word_t frame;

	if (allocLimit < 0) {
		dprintf(0, "!!! allocLimit somehow negative!?\n");
		frame = NULLFRAME;
	} else if (allocLimit == 0) {
		dprintf(0, "*** allocLimit reached\n");
		frame = NULLFRAME;
	} else {
		frame = doFrameAlloc();
		allocLimit--;
		addFrameList(frame);
		dprintf(0, "*** allocated %p, limit now %d\n", frame, allocLimit);
	}

	return frame;
}

L4_Word_t
kframe_alloc(void) {
	return doFrameAlloc();
}

static L4_Word_t
deleteFrameList(void) {
	assert(allocHead != NULL);
	assert(allocLast != NULL);

	FrameList *oldAllocHead = allocHead;
	L4_Word_t frame;

	if ((allocHead->frame & REFBIT_MASK) == 0) {
		// Not been referenced, this is the frame to swap
		frame = allocHead->frame;
		allocHead = allocHead->next;
		free(oldAllocHead);
	} else {
		// Been referenced, clear and move to back
		frame = 0;
		allocHead = allocHead->next;
		allocLast->next = oldAllocHead;
		oldAllocHead->next = NULL;
	}

	return frame;
}

L4_Word_t
frame_nextswap(void) {
	L4_Word_t frame;

	// Eventually it will find something
	while ((frame = deleteFrameList()) == 0);

	return frame;
}

void
frame_free(L4_Word_t frame)
{
	*((L4_Word_t*) frame) = firstFree;
	firstFree = frame;
}

