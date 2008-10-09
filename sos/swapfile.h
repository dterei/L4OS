#ifndef _SWAPFILE_H
#define _SWAPFILE_H

#include <sos/sos.h>

#include "l4.h"

extern fildes_t swapfile;

// Initialise swapfile, including opening it
void swapfile_init(void);

void swapfile_open(void);
void swapfile_close(void);

int swapfile_usage(void);

// Allocate a new slow in the swapfile
L4_Word_t swapslot_alloc(void);

// Free an allocated slow in the swapfile
int swapslot_free(L4_Word_t slot);

#endif // _SWAPFILE_H
