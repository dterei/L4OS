#ifndef _NETWORK_H
#define _NETWORK_H

#include <nfs/nfs.h>
#include <serial/serial.h>


// Always call network_irq if an interrupt occurs that you are not interested in
int network_irq(L4_ThreadId_t *tP, int *sendP);
void network_init(void);
void network_flush(void);
int network_puts(char *s, int len);
int network_register_serialhandler(void (*handler)(struct serial *serial, char c));
struct cookie mnt_point;

#endif
