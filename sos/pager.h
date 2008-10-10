#ifndef _PAGER_H
#define _PAGER_H

#include <sos/sos.h>

#include "l4.h"

extern L4_ThreadId_t virtual_pager;

typedef struct Pagetable1 Pagetable;

Pagetable *pagetable_init(void);
void pager_init(void);
void sos_pager_handler(L4_ThreadId_t tid, L4_Msg_t *msg);
char *pager_buffer(L4_ThreadId_t tid);
int memory_usage(void);

#endif // _PAGER_H

