#ifndef _PAGER_H
#define _PAGER_H

#include <sos/sos.h>

#include "l4.h"

typedef struct Pagetable1 Pagetable;

Pagetable *pagetable_init(void);
void pager_init(void);
L4_ThreadId_t pager_get_tid(void);
int pager_is_active(void);
char *pager_buffer(L4_ThreadId_t tid);

#endif // sos/pager.h

