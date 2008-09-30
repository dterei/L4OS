#include <sos/sos.h>
#include <stdio.h>
#include <stdlib.h>

#include "hex.h"

#define MAX_ROW 32

int hex(int argc, char **argv) {
	int offset, size, width, r;
	char buf[MAX_ROW];

	if (argc < 4) {
		printf("usage: %s file size offset [width]\n", argv[0]);
		return 1;
	}

	fildes_t f = open(argv[1], FM_READ);
	size = atoi(argv[2]);
	offset = atoi(argv[3]);

	if (argc == 5) {
		width = atoi(argv[4]);
	} else {
		width = MAX_ROW;
	}

	lseek(f, offset, SEEK_SET);

	for (int row = 0; row < size; row += width) {
		printf("%04x:", row);
		r = read(f, buf, width);

		for (int pad = r; pad < width; pad++) {
			buf[pad] = 0;
		}

		for (int col = 0; col < width; col += 2) {
			printf(" %02x%02x", buf[col] & 0x000000ff, buf[col+1] & 0x000000ff);
		}

		printf("\n");
	}

	close(f);

	return 0;
}

