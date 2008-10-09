#include <sos/sos.h>

#include "top.h"
#include "sosh.h"

int top(int argc, char **argv) {
	printf("Mem: %d pages\n", memuse());
	printf("Swap: %d pages\n", swapuse());
	return 1;
}

