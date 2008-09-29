#include <sos/sos.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "sosh.h"

#define SIZE 4096

int alloc(int argc, char **argv) {
	int written;
	char *buf = (char*) malloc(SIZE);

	printf("memory allocated at %p (%u)\n", buf, (unsigned int) buf);
	
	written = 0;

	for (int i = 1; i < argc; i++) {
		strcpy(buf + written, argv[i]);
		written += strlen(argv[i]);

		if (i < argc - 1) {
			buf[written] = ' ';
			written++;
		}
	}

	for (int i = written; i < SIZE; i++) {
		buf[i] = 0;
	}

	return 0;
}

