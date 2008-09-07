#ifndef _PAGER_H
#define _PAGER_H

#include <l4/types.h>
#include <l4/message.h>
#include <sos/sos.h>

// XXX Hack: should look this up in USER_HW_VALID_PGSIZES
#define PAGESIZE 4096

#define PAGETABLE_SIZE1 1024
#define PAGETABLE_SIZE2 1024
#define PAGEALIGN ~(PAGESIZE -1)

/* Access rights */
#define REGION_READ 0x4
#define REGION_WRITE 0x2
#define REGION_EXECUTE 0x1

/* Page Table Struct */
typedef struct {
	L4_Word_t pages[PAGETABLE_SIZE2];
} PageTable2;

typedef struct {
	PageTable2 *pages2[PAGETABLE_SIZE1];
} PageTable1;

/* Region Struct */
typedef enum {
	REGION_STACK,
	REGION_HEAP,
	REGION_OTHER,
	REGION_THREAD_INIT
} region_type;

struct Region {
	region_type type; // type of region (heap? stack?)
	uintptr_t base;   // base of the region
	uintptr_t size;   // size of region
	int mapDirectly;  // do we directly (1:1) map it?
	int rights;       // access rights of region (read, write, execute)
	int id;           // what bootinfo uses to identify the region
	struct Region *next;
};

typedef struct Region Region;

/* Address space Struct */
typedef struct {
	PageTable1 *pagetb;
	Region *regions;
} AddrSpace;

/* Max of 256 Address Spaces */
extern AddrSpace addrspace[MAX_ADDRSPACES];

uintptr_t add_stackheap(AddrSpace *as);
void pager_init(void);
void pager(L4_ThreadId_t tid, L4_Msg_t *msg);
void pager_flush(L4_ThreadId_t tid, L4_Msg_t *msgP);
int sos_moremem(uintptr_t *base, unsigned int nb);
L4_Word_t *sender2kernel(L4_Word_t addr);

// XXX
//
// typedef struct userptr_t userptr_t;
//
// L4_Word_t *user2kernel(userptr addr);
// userptr new_userptr(void *ptr, rights r);

#endif // _PAGER_H

