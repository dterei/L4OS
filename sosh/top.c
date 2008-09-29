#include <sos/sos.h>

#include "top.h"
#include "sosh.h"

int top(int argc, char **argv) {
	printf("memory in use: %d pages\n", memuse());
	return 1;
}

