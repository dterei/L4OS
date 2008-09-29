#include <sos/sos.h>
#include <stdio.h>
#include <string.h>

#include "memdump.h"
#include "sosh.h"

#define SIZE 4096
#define BLOCK 256

int memdump(int argc, char **argv) {
	if (argc < 3) {
		printf("usage: %s address file\n", argv[0]);
		return 1;
	}

	char *dump = (char*) atoi(argv[1]);

	fildes_t out = open(argv[2], FM_WRITE);

	for (int nWritten = 0; nWritten < SIZE; nWritten++) {
		nWritten += write(out, dump + nWritten, BLOCK - 1);
		printf("memdump: writen %d bytes\n", nWritten);
	}

	close(out);

	return 0;
}

