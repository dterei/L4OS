#ifndef _PAGER_H
#define _PAGER_H

#include <l4/types.h>
#include <l4/message.h>

#define MAX_ADDRSPACES 256
#define PAGETABLE_SIZE1 1024
#define PAGETABLE_SIZE2 1024

/* Page Table Struct */
typedef struct {
	L4_Word_t *pages[PAGETABLE_SIZE2];
} PageTable2;

typedef struct {
	PageTable2 *pages2[PAGETABLE_SIZE1];
} PageTable;

/* Region Struct */
struct Region {
	uintptr_t vbase; // start of region in virtual address space/page table
	uintptr_t vsize; // size of region " "
	uintptr_t pbase; // start of region in physical memory / frame table
	uintptr_t psize; // size of region " "
	int rights;      // access rights of region (read, write, execute)
	int id;          // what bootinfo uses to identify the region
	struct Region *next; // next region in list;
};

typedef struct Region Region;

/* Address space Struct */
typedef struct {
	PageTable *pagetb;
	Region *regions;
} AddrSpace;

/* Max of 256 Address Spaces */
extern AddrSpace addrspace[MAX_ADDRSPACES];

extern void pager(L4_ThreadId_t tid, L4_Msg_t *msg);

#endif // _PAGER_H
