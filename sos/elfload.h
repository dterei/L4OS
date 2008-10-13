#ifndef _SOS_ELFLOAD_H_
#define _SOS_ELFLOAD_H_

#include "l4.h"

/**
 * Thread to handle the loading and execution of ELF files.
 */

void elfload_init(void);
L4_ThreadId_t elfload_get_tid(void);

#endif

