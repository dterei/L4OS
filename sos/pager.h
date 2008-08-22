#ifndef _PAGER_H
#define _PAGER_H

#include <l4/types.h>
#include <l4/message.h>

#define MAX_ADDRSPACES 256
#define PAGETABLE_SIZE1 1024
#define PAGETABLE_SIZE2 1024

/* Page Table Struct */
typedef struct {
	L4_Word_t pages[PAGETABLE_SIZE2];
} PageTable2;

typedef struct {
	PageTable2 *pages2[PAGETABLE_SIZE1];
} PageTable1;

/* Region Struct */
struct Region {
	uintptr_t vbase; // start of region in virtual address space/page table
	uintptr_t pbase; // start of region in physical memory / frame table
	uintptr_t size;  // size of region
	int rights;      // access rights of region (read, write, execute)
	int id;          // what bootinfo uses to identify the region
	struct Region *next; // next region in list;
};

typedef struct Region Region;

/* Access rights */
#define REGION_READ 0x4
#define REGION_WRITE 0x2
#define REGION_EXECUTE 0x1

/* Address space Struct */
typedef struct {
	PageTable1 *pagetb;
	Region *regions;
} AddrSpace;

/* Max of 256 Address Spaces */
extern AddrSpace addrspace[MAX_ADDRSPACES];

void as_init(void);
void init_bootmem(AddrSpace *as);
uintptr_t add_stackheap(AddrSpace *as);
uintptr_t phys2virt(AddrSpace *as, uintptr_t phys);
void pager(L4_ThreadId_t tid, L4_Msg_t *msg);

#endif // _PAGER_H
