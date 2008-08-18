/****************************************************************************
 *
 *      $Id: pager.c,v 1.4 2003/08/06 22:52:04 benjl Exp $
 *
 *      Description: Example pager for the SOS project.
 *
 *      Author:			Godfrey van der Linden
 *      Original Author:	Ben Leslie
 *
 ****************************************************************************/


//
// Pager is called from the syscall loop whenever a page fault occurs. The
// current implementation simply maps whichever pages are asked for.
//
#include <stdio.h>

#include <l4/types.h>

#include <l4/map.h>
#include <l4/misc.h>
#include <l4/space.h>
#include <l4/thread.h>

#include "pager.h"
#include "libsos.h"

/* Page Table Struct */
typedef struct {
	L4_Word_t *pages[1024];
} PageTable2;

typedef struct {
	PageTable2 *pages2[1024];
} PageTable;

/* Address space Struct */
typedef struct {
	PageTable *pagetb;
	L4_Word_t *regions;
} AddrSpace;


/* Max of 256 Address Spaces */
static AddrSpace addrspace[256];

void
pager(L4_ThreadId_t tid, L4_Msg_t *msgP)
{
    // Get the faulting address
    L4_Word_t addr = L4_MsgWord(msgP, 0) & ~(PAGESIZE-1);
    L4_Word_t ip = L4_MsgWord(msgP, 1);

    // Construct fpage IPC message
    L4_Fpage_t targetFpage = L4_FpageLog2(addr, 12);
    L4_Set_Rights(&targetFpage, L4_FullyAccessible);

    // Assumes virt - phys 1-1 mapping
    L4_PhysDesc_t phys = L4_PhysDesc(addr, L4_DefaultMemory);

    if ( !L4_MapFpage(L4_SenderSpace(), targetFpage, phys) ) {
	sos_print_error(L4_ErrorCode());
	printf(" Can't map page at %lx for tid %lx, ip = %lx\n", addr, tid.raw, ip);
    }
}

