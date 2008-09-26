#ifndef _SWAPFILE_H
#define _SWAPFILE_H

#include <sos/sos.h>

#include "l4.h"

extern fildes_t swapfile;

void swapfile_init(void);

L4_Word_t get_swapslot(void);

int free_swapslot(L4_Word_t slot);

#endif // _SWAPFILE_H
