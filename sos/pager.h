#ifndef _PAGER_H
#define _PAGER_H

#include <l4/types.h>
#include <l4/message.h>

/* Page Table Struct */
typedef struct {
	L4_Word_t *pages[1024];
} PageTable2;

typedef struct {
	PageTable2 *pages2[1024];
} PageTable;

/* Region Struct */
struct Region {
	uintptr_t vbase; // start of region in virtual address space/page table
	uintptr_t vsize; // size of region " "
	uintptr_t pbase; // start of region in physical memory / frame table
	uintptr_t psize; // size of region " "
	int rights; // access rights of region (read, write, execute)
	struct Region *next; // next region in list;
};

typedef struct Region Region;

/* Address space Struct */
typedef struct {
	PageTable *pagetb;
	Region *regions;
} AddrSpace;


/* Max of 256 Address Spaces */
extern AddrSpace addrspace[256];

extern void pager(L4_ThreadId_t tid, L4_Msg_t *msg);

#endif // _PAGER_H

