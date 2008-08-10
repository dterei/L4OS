#include <nfs/nfs.h>

// Always call network_irq if an interrupt occurs that you are not interested in
extern void network_irq(L4_ThreadId_t *tP, int *sendP);
extern void network_init(void);
extern void network_sendstring(int, int*);
extern struct cookie mnt_point;
