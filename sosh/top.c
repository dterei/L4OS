#include <sos/sos.h>

#include "top.h"
#include "sosh.h"

int top(int argc, char **argv) {
	printf("Mem: %d\n", memuse());
	printf("Phys: %d\n", physuse());
	printf("Swap: %d\n", swapuse());
	return 1;
}

