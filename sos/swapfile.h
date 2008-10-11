#ifndef _SWAPFILE_H
#define _SWAPFILE_H

#include <sos/sos.h>

#include "l4.h"

typedef struct Swapfile_t Swapfile;

// Initialise the default swapfile
void swapfile_init_default(void);

// Get the default swapfile
Swapfile *swapfile_default(void);

// Create a new swapfile bookkeeping struct
Swapfile *swapfile_init(fildes_t fd);

// Get the slots in use by a swap file
int swapfile_get_usage(Swapfile *sf);

// Get the file descriptor used by a swap file
fildes_t swapfile_get_fd(Swapfile *sf);

// Allocate a new slot in the swapfile
L4_Word_t swapslot_alloc(Swapfile *sf);

// Free an allocated slot in the swapfile
void swapslot_free(Swapfile *sf, L4_Word_t slot);

#endif // sos/swapfile.h
