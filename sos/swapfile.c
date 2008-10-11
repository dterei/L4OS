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
#include "vfs.h"

#define verbose 1

typedef struct SwapfileData_t SwapfileData;
struct SwapfileData_t {
	fildes_t fd;
	L4_Word_t head;
	int usage;
};

#define SWAPSIZE ((PAGESIZE - sizeof(SwapfileData)) / sizeof(L4_Word_t))

struct Swapfile_t {
	SwapfileData data;
	L4_Word_t slots[SWAPSIZE];
};

static Swapfile *defaultSwapfile;

void swapfile_init_default(void) {
	// Open the file
	L4_Msg_t msg;
	fildes_t fd;

	strcpy(pager_buffer(pager_get_tid()), SWAPFILE_FN);

	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, (L4_Word_t) FM_READ | FM_WRITE);
	fd = syscall(L4_rootserver, SOS_OPEN, YES_REPLY, &msg);
	dprintf(1, "*** swapfile_init_default: opened at %d\n", fd);

	// Create the bookkeeping
	defaultSwapfile = swapfile_init(fd);
}

Swapfile *swapfile_default(void) {
	return defaultSwapfile;
}

Swapfile *swapfile_init(fildes_t fd) {
	assert(sizeof(Swapfile) == PAGESIZE);
	Swapfile *sf = (Swapfile*) frame_alloc();

	for (int i = 0; i < SWAPSIZE - 1; i++) {
		sf->slots[i] = i + 1;
	}

	sf->slots[SWAPSIZE - 1] = ADDRESS_NONE;

	sf->data.fd = fd;
	sf->data.head = 0;
	sf->data.usage = 0;

	return sf;
}

int swapfile_get_usage(Swapfile *sf) {
	return sf->data.usage;
}

fildes_t swapfile_get_fd(Swapfile *sf) {
	return sf->data.fd;
}

L4_Word_t swapslot_alloc(Swapfile *sf) {
	assert(swapfile_get_usage(sf) <= SWAPSIZE);

	L4_Word_t slot = sf->data.head;
	sf->data.head = sf->slots[slot];
	sf->data.usage++;

	return slot * PAGESIZE;
}

void swapslot_free(Swapfile *sf, L4_Word_t slot) {
	assert((slot & ~PAGEALIGN) == 0);
	assert(swapfile_get_usage(sf) > 0);

	slot /= PAGESIZE;

	sf->slots[slot] = sf->data.head;
	sf->data.head = slot;
	sf->data.usage--;
}

