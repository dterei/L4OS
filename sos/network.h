#ifndef _NETWORK_H
#define _NETWORK_H

#include <nfs/nfs.h>
#include <serial/serial.h>


// Always call network_irq if an interrupt occurs that you are not interested in
extern void network_irq(L4_ThreadId_t *tP, int *sendP);
extern void network_init(void);
extern int network_sendstring_int(int, int*);
extern int network_sendstring_char(int len, char *contents);
extern int network_register_serialhandler(void (*handler)(struct serial *serial, char c));
extern struct cookie mnt_point;

#endif

