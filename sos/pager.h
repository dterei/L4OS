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
	uintptr_t base; // base of the region
	uintptr_t size; // size of region
	int mapDirectly;  // do we directly (1:1) map it?
	int rights;     // access rights of region (read, write, execute)
	int id;         // what bootinfo uses to identify the region
	struct Region *next;
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

uintptr_t add_stackheap(AddrSpace *as);
void as_init(void);
void pager(L4_ThreadId_t tid, L4_Msg_t *msg);

#endif // _PAGER_H
