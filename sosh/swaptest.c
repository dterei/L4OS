#include <sos/sos.h>
#include <stdio.h>
#include <stdlib.h>

#include "alloc.h"
#include "cp.h"
#include "hex.h"
#include "rm.h"
#include "swaptest.h"

// This assumes that there are 2 free pages on boot
int swaptest(int argc, char **argv) {
	char *newArgv[5];

	// These will be put in memory as usual
	printf("allocating 12345 and 67890\n");

	newArgv[0] = "alloc";
	newArgv[1] = "12345";
	alloc(2, newArgv);

	newArgv[1] = "67890";
	alloc(2, newArgv);

	// This should kick out the stack of process 6
	printf("allocating abcde\n");
	newArgv[1] = "abcde";
	alloc(2, newArgv);

	// This should kick out the 12345 alloc
	printf("allocating fghij\n");
	newArgv[1] = "fghij";
	alloc(2, newArgv);

	// This should contain the 12345 on the 3rd line.
	// The first two lines (32 bytes) are internal malloc bookkeeping
	printf("First 32 bytes of the second-swapped page:\n");

	newArgv[0] = "hex";
	newArgv[1] = ".swap";
	newArgv[2] = "48";
	newArgv[3] = "4096";
	newArgv[4] = "16";
	hex(5, newArgv);

	// Need to remove while the write/close bug exists
	printf("removing copied swap file\n");

	newArgv[0] = "rm";
	newArgv[1] = "swap";
	rm(2, newArgv);

	printf("copying swap file\n");

	newArgv[0] = "cp";
	newArgv[1] = ".swap";
	newArgv[2] = "swap";
	cp(3, newArgv);

	printf("Same section of the copied page:\n");

	newArgv[0] = "hex";
	newArgv[1] = "swap";
	newArgv[2] = "48";
	newArgv[3] = "4096";
	newArgv[4] = "16";
	hex(5, newArgv);

	return 1;
}

