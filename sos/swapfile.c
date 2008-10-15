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
	char path[MAX_FILE_NAME];
	fildes_t fd;
	L4_Word_t head;
	int usage;
};

#define SWAPSIZE ((PAGESIZE - sizeof(SwapfileData)) / sizeof(L4_Word_t))

struct Swapfile_t {
	SwapfileData data;
	L4_Word_t slots[SWAPSIZE];
};

Swapfile *swapfile_init(char *path) {
	dprintf(1, "*** swapfile_init path=%s\n", path);
	assert(sizeof(Swapfile) == PAGESIZE);
	Swapfile *sf;

	sf = (Swapfile*) frame_alloc();

	for (int i = 0; i < SWAPSIZE - 1; i++) {
		sf->slots[i] = i + 1;
	}

	sf->slots[SWAPSIZE - 1] = ADDRESS_NONE;

	sf->data.fd = VFS_NIL_FILE;
	sf->data.head = 0;
	sf->data.usage = 0;
	strncpy(sf->data.path, path, MAX_FILE_NAME);

	return sf;
}

void swapfile_open(Swapfile *sf, int rights) {
	dprintf(1, "*** swapfile_open path=%s rights=%d\n", sf->data.path, rights);
	assert(sf->data.fd == VFS_NIL_FILE);
	L4_Msg_t msg;

	strcpy(pager_buffer(pager_get_tid()), sf->data.path);

	syscall_prepare(&msg);
	L4_MsgAppendWord(&msg, rights | FM_NOTRUNC);

	syscall(L4_rootserver, SOS_OPEN, NO_REPLY, &msg);
}

int swapfile_is_open(Swapfile *sf) {
	return (sf->data.fd != VFS_NIL_FILE);
}

void swapfile_close(Swapfile *sf) {
	dprintf(1, "*** swapfile_close path=%s\n", sf->data.path);
	assert(sf->data.fd != VFS_NIL_FILE);
	closeNonblocking(sf->data.fd);
	sf->data.fd = VFS_NIL_FILE;
}

int swapfile_get_usage(Swapfile *sf) {
	return sf->data.usage;
}

fildes_t swapfile_get_fd(Swapfile *sf) {
	assert(swapfile_is_open(sf));
	return sf->data.fd;
}

void swapfile_set_fd(Swapfile *sf, fildes_t fd) {
	dprintf(1, "*** swapfile_get_fd path=%s fd=%d\n", sf->data.path, fd);
	assert(sf->data.fd == VFS_NIL_FILE);
	assert(fd != VFS_NIL_FILE);
	sf->data.fd = fd;
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

