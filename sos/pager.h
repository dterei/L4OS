#ifndef _PAGER_H
#define _PAGER_H

#include <l4/types.h>
#include <l4/message.h>
#include <sos/sos.h>

// Should look this up in USER_HW_VALID_PGSIZES
#define PAGESIZE 4096
#define PAGEALIGN (~((PAGESIZE) - 1))

#define PAGETABLE_SIZE1 1024
#define PAGETABLE_SIZE2 1024

/* Access rights */
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

// The thread id of the main pager process
extern L4_ThreadId_t virtual_pager;

Region *region_init(region_type type, uintptr_t base,
		uintptr_t size, int rights, int dirmap);
uintptr_t region_base(Region *r);
uintptr_t region_size(Region *r);
Region *region_next(Region *r);
void region_set_rights(Region *r, int rights);
void region_append(Region *r, Region *toAppend);

PageTable *pagetable_init(void);
void pager_init(void);
void pager_flush(L4_ThreadId_t tid, L4_Msg_t *msgP);
int sos_moremem(uintptr_t *base, unsigned int nb);
L4_Word_t *sender2kernel(L4_Word_t addr);
void sos_pager_handler(L4_Word_t addr, L4_Word_t ip);

// XXX
//
// typedef struct userptr_t userptr_t;
//
// L4_Word_t *user2kernel(userptr addr);
// userptr new_userptr(void *ptr, rights r);

#endif // _PAGER_H

