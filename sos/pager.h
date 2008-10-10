#ifndef _PAGER_H
#define _PAGER_H

#include <sos/sos.h>

#include "l4.h"

// Pager-related structures and data
typedef struct Pagetable1 Pagetable;

#define REGION_READ 0x4
#define REGION_WRITE 0x2
#define REGION_EXECUTE 0x1

typedef enum {
	REGION_STACK,
	REGION_HEAP,
	REGION_OTHER,
	REGION_THREAD_INIT
} region_type;

typedef struct Region_t Region;

// The thread id of the main pager process
extern L4_ThreadId_t virtual_pager;

Region *region_init(region_type type, uintptr_t base,
		uintptr_t size, int rights, int dirmap);
uintptr_t region_base(Region *r);
uintptr_t region_size(Region *r);
Region *region_next(Region *r);
void region_set_rights(Region *r, int rights);
void region_append(Region *r, Region *toAppend);

Pagetable *pagetable_init(void);

void pager_init(void);
void sos_pager_handler(L4_ThreadId_t tid, L4_Msg_t *msg);
char *pager_buffer(L4_ThreadId_t tid);

int memory_usage(void);

#endif // _PAGER_H

