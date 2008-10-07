#include <sos/sos.h>
#include <stdio.h>
#include <stdlib.h>

#include "alloc.h"
#include "cp.h"
#include "hex.h"
#include "memdump.h"
#include "rm.h"
#include "swaptest.h"

// This assumes that there are 2 free pages on boot
int swaptest(int argc, char **argv) {
	char *newArgv[5];
	char *buf = malloc(sizeof(char) * 20);
	int target;

	// These will be put in memory as usual
	printf("allocating 1234\n");
	newArgv[0] = "alloc";
	newArgv[1] = "1234";
	target = alloc(2, newArgv);

	printf("allocating qwer\n");
	newArgv[1] = "qwer";
	alloc(2, newArgv);

	// This should kick out the stack of process 5
	printf("allocating asdf\n");
	newArgv[1] = "asdf";
	alloc(2, newArgv);

	// Which will in turn kick out the stack of process
	// 6, since the stack of process 5 is needed

	// This should kick out the 1234 alloc
	printf("allocating zxcv\n");
	newArgv[1] = "zxcv";
	alloc(2, newArgv);

	// This should bring back in the 1234 alloc and
	// kick out the qwer alloc, then write the 1234 alloc
	// to a file called "dump".  Note that this doesn't
	// work (which is why I'm testing it).
	printf("dumping 1234\n");

	// oh yeah remove it first
	newArgv[0] = "rm";
	newArgv[1] = "dump";
	rm(2, newArgv);

	newArgv[0] = "memdump";
	sprintf(buf, "%u", (unsigned int) target);
	newArgv[1] = buf;
	newArgv[2] = "dump";
	memdump(3, newArgv);

	/*
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
	*/

	return 1;
}

