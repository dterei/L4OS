#include <stdio.h>

#include "irq.h"
#include "l4.h"
#include "libsos.h"

#define MAX_IRQ 32

// Array of irq request info - easy
static struct IrqInfo_t irq_info[MAX_IRQ];

// Initialise irq stuff
void irq_init(void) {
	L4_Set_NotifyMask(1 << SOS_IRQ_NOTIFY_BIT);

	for (int i = 0; i < MAX_IRQ; i++) {
		irq_info[i].irq = (-1);
	}
}

// Register a driver
void irq_add(int irq, int (*irq_request)(L4_ThreadId_t*, int*)) {
	if (irq < 0 || irq >= MAX_IRQ) {
		printf("!!! irq_add: irq (%d) was out of range (0..%d)\n", irq, MAX_IRQ);
	} else {
		irq_info[irq].irq = irq;
		irq_info[irq].irq_request = irq_request;
	}
}

// Find a driver with a given irq
IrqInfo irq_find(int irq) {
	if (irq_info[irq].irq == (-1)) {
		printf("!!! irq_find: irq (%d) not found\n", irq);
		return NULL;
	} else {
		return &irq_info[irq];
	}
}

