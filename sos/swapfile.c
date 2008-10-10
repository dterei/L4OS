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

#include <string.h>

#include "constants.h"
#include "frames.h"
#include "l4.h"
#include "libsos.h"
#include "pager.h"
#include "swapfile.h"

#define verbose 1

#define SWAPFILE_FN ".swap"
#define SWAPSIZE (PAGESIZE / sizeof(L4_Word_t))

fildes_t swapfile;

static L4_Word_t *fileSlots;
static L4_Word_t firstFree;
static L4_Word_t totalInUse;

void
swapfile_init(void)
{
	fileSlots = (L4_Word_t*) frame_alloc();

	for (int i = 0; i < SWAPSIZE - 1; i++) {
		fileSlots[i] = i + 1;
	}

	fileSlots[SWAPSIZE - 1] = ADDRESS_NONE;
	firstFree = 0;
	totalInUse = 0;

	swapfile_open();
}

void swapfile_open(void) {
	L4_Msg_t msg;

	dprintf(2, "*** swapfile_init: opening swap file\n");
	strcpy(pager_buffer(pager_get_tid()), SWAPFILE_FN);
	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) FM_READ | FM_WRITE);
	swapfile = syscall(L4_rootserver, SOS_OPEN, YES_REPLY, &msg);
	dprintf(2, "*** swapfile_init: opened swapfile, fd=%d\n", swapfile);
}

void swapfile_close(void) {
	L4_Msg_t msg;

	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) swapfile);
	syscall(L4_rootserver, SOS_CLOSE, YES_REPLY, &msg);
	swapfile = (-1);
}

int swapfile_usage(void) {
	return totalInUse;
}

L4_Word_t swapslot_alloc(void) {
	assert(totalInUse <= SWAPSIZE);

	L4_Word_t slot = firstFree;
	firstFree = fileSlots[slot];
	totalInUse++;

	return slot * PAGESIZE;
}

int swapslot_free(L4_Word_t slot) {
	assert((slot & ~PAGEALIGN) == 0);
	assert(totalInUse > 0);

	slot /= PAGESIZE;

	fileSlots[slot] = firstFree;
	firstFree = slot;
	totalInUse--;

	return 0;
}

