#ifndef _SWAPFILE_H
#define _SWAPFILE_H

#include <sos/sos.h>

#include "l4.h"

// TODO one per process
extern fildes_t swapfile;

// Initialise swapfile, including opening it
void swapfile_init(void);

// Open/close the swapfile TODO one per process
void swapfile_open(void);
void swapfile_close(void);

// Get the number of slots currently in use TODO one per process
int swapfile_usage(void);

// Allocate a new slow in the swapfile
L4_Word_t swapslot_alloc(void);

// Free an allocated slow in the swapfile
int swapslot_free(L4_Word_t slot);

#endif // _SWAPFILE_H
