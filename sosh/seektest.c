#include <sos/sos.h>
#include <stdio.h>

#include "seektest.h"
#include "sosh.h"

#define FILENAME "seeker"
#define SIZE 64

int seektest(int argc, char **argv) {
	char buf1[4 * SIZE];
	char buf2[4 * SIZE];
	int *ibuf1 = (int*) buf1;
	int *ibuf2 = (int*) buf2;
	int tmp, passed;

	printf("removing %s\n", FILENAME);
	fremove(FILENAME);

	printf("opening %s\n", FILENAME);
	fildes_t fd = open(FILENAME, FM_READ | FM_WRITE);

	printf("filling buffer\n");
	for (int i = 0; i < SIZE; i++) {
		ibuf1[i] = i;
	}

	printf("writing buffer\n");
	tmp = write(fd, buf1, 4 * SIZE);
	printf("bytes written: %d\n", tmp);

	printf("seeking\n");
	tmp = lseek(fd, 0, SEEK_SET);
	printf("seek result: %d\n", tmp);

	printf("reading buffer\n");
	tmp = read(fd, buf2, 4 * SIZE);
	printf("bytes read %d\n", tmp);

	printf("closing file\n");
	close(fd);

	passed = 1;

	printf("checking\n");
	for (int i = 0; i < SIZE; i++) {
		if (ibuf1[i] != ibuf2[i]) {
			printf("%4d: %4d != %4d\n", i, ibuf1[i], ibuf2[i]);
			passed = 0;
		}
	}

	if (passed) {
		printf("passed\n");
	} else {
		printf("failed\n");
	}

	return 0;
}

