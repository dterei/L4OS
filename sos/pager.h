#ifndef _PAGER_H
#define _PAGER_H

#include <sos/sos.h>

#include "l4.h"

#define PAGESIZE 4096
#define PAGEWORDS ((PAGESIZE) / (sizeof(L4_Word_t)))
#define PAGEALIGN (~((PAGESIZE) - 1))

// Access rights
#define REGION_READ 0x4
#define REGION_WRITE 0x2
#define REGION_EXECUTE 0x1

// Pager-related structures and data
typedef struct PageTable1 PageTable;

typedef enum {
	REGION_STACK,
	REGION_HEAP,
	REGION_OTHER,
	REGION_THREAD_INIT
} region_type;

typedef struct Region_t Region;

typedef struct PagerRequest_t PagerRequest;

// The thread id of the main pager process
extern L4_ThreadId_t virtual_pager;

Region *region_init(region_type type, uintptr_t base,
		uintptr_t size, int rights, int dirmap);
uintptr_t region_base(Region *r);
uintptr_t region_size(Region *r);
Region *region_next(Region *r);
void region_set_rights(Region *r, int rights);
void region_append(Region *r, Region *toAppend);
void region_free_all(Region *r);

PageTable *pagetable_init(void);
void pagetable_free(PageTable *pt);

void pager_init(void);
void pager_flush(L4_ThreadId_t tid, L4_Msg_t *msgP);
void sos_pager_handler(L4_Word_t addr, L4_Word_t ip);

int sos_moremem(uintptr_t *base, unsigned int nb);

void copyIn(L4_ThreadId_t tid, void *src, size_t size, int append);
void copyOut(L4_ThreadId_t tid, void *dest, size_t size, int append);
char *pager_buffer(L4_ThreadId_t tid);

#endif // _PAGER_H

