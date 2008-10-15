#ifndef _SWAPFILE_H
#define _SWAPFILE_H

#include <sos/sos.h>

#include "l4.h"

typedef struct Swapfile_t Swapfile;

// Create a new swapfile bookkeeping struct with given rights -
// useful for when dealing with swapfile ELF structs
Swapfile *swapfile_init(char *path);

// Open a swapfile (non blocking)
void swapfile_open(Swapfile *sf, int rights);

// Test if a swapfile is open (compares to fd)
int swapfile_is_open(Swapfile *sf);

// Close a swapfile (non blocking)
void swapfile_close(Swapfile *sf);

// Get the slots in use by a swap file
int swapfile_get_usage(Swapfile *sf);

// Get the file descriptor used by a swap file, (-1) if not open
fildes_t swapfile_get_fd(Swapfile *sf);

// Set the file descriptor (only a good idea after opening)
void swapfile_set_fd(Swapfile *sf, fildes_t fd);

// Allocate a new slot in the swapfile
L4_Word_t swapslot_alloc(Swapfile *sf);

// Free an allocated slot in the swapfile
void swapslot_free(Swapfile *sf, L4_Word_t slot);

#endif // sos/swapfile.h
