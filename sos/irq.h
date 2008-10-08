#ifndef _SOS_IRQ_H
#define _SOS_IRQ_H

#include "l4.h"

typedef struct IrqInfo_t *IrqInfo;
struct IrqInfo_t {
	// Properties
	int irq;

	// Callbacks
	int (*irq_request)(L4_ThreadId_t *tid, int *send);
};

// Initialise irq stuff
void irq_init(void);

// Register a driver
void irq_add(int irq, int (*irq_request)(L4_ThreadId_t *tid, int *send));

// Find a driver with a given irq
IrqInfo irq_find(int irq);

#endif // sos/irq.h
