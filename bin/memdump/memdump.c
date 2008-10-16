#include <sos/sos.h>
#include <stdio.h>
#include <string.h>

#define SIZE 4096
#define BLOCK 512

int main(int argc, char *argv[]) {
	if (argc < 3) {
		printf("usage: %s address file\n", argv[0]);
		return 1;
	}

	printf("Start memdump\n");

	char *dump = (char*) (atoi(argv[1]) & ~(SIZE - 1));

	if (dump == NULL) {
		printf("must give in decimal form\n");
		return 1;
	} else {
		printf("dumping %p\n", dump);
	}

	fildes_t out = open(argv[2], FM_WRITE);

	// touch it first
	int nWritten = write(out, dump, BLOCK);
	printf("physically at %p\n", (void*) memloc((L4_Word_t) dump));

	while (nWritten < SIZE) {
		nWritten += write(out, dump + nWritten, BLOCK);
		printf("memdump: writen %d bytes\n", nWritten);
	}

	close(out);

	return 0;
}

