#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <sos/sos.h>

#include "pt_test.h"
#include "sosh.h"

#define NUM_SLOTS 128
#define SLOTSIZE 196
#define STACKSIZE 512

typedef int test_t;

int pt_test(int argc, char **argv) {
	int i, j, passed;
	test_t **buf;
	test_t stack[STACKSIZE];

	// Test heap

	printf("allocating heap\n");

	buf = (test_t**) malloc(NUM_SLOTS * sizeof(test_t*));

	for (i = 0; i < NUM_SLOTS; i++) {
		buf[i] = (test_t*) malloc(SLOTSIZE * sizeof(test_t));

		if (i % 16 == 0) {
			printf("%d... ", i / 16);
			flush(stdout_fd);
		}
	}

	printf("\nfilling heap with first test values\n");

	for (i = 0; i < NUM_SLOTS; i++) {
		if (i % 16 == 0) {
			printf("%d... ", i / 16);
			flush(stdout_fd);
		}

		for (j = 0; j < SLOTSIZE; j++) {
			buf[i][j] = i*i + j;
		}
	}

	printf("\ntesting heap\n");

	// Do in two rounds in case memory is going crazy

	for (i = 0; i < NUM_SLOTS; i++) {
		passed = 1;

		if (i % 16 == 0) {
			printf("%d... ", i / 16);
			flush(stdout_fd);
		}

		for (j = 0; j < SLOTSIZE; j++) {
			if (buf[i][j] != i*i + j) {
				printf("pt_test: failed for i=%d j=%d (%d vs %d)\n",
						i, j, buf[i][i], i*i + j);
				passed = 0;
			}
		}
	}

	// Try a different touch value

	printf("\ntesting for a second time\n");

	for (i = 0; i < NUM_SLOTS; i++) {
		for (j = 0; j < SLOTSIZE; j++) {
			buf[i][j] = (i - 1) * (i - 2) * (j + 1);
		}

		if (i % 16 == 0) {
			printf("%d... ", i / 16);
			flush(stdout_fd);
		}
	}

	// And again

	printf("\nverifing\n");

	for (i = 0; i < NUM_SLOTS; i++) {
		passed = 1;

		if (i % 16 == 0) {
			printf("%d... ", i / 16);
			flush(stdout_fd);
		}

		for (j = 0; j < SLOTSIZE; j++) {
			if (buf[i][j] != (i - 1) * (i - 2) * (j + 1)) {
				printf("pt_test: failed for i=%d j=%d (%d vs %d)\n",
						i, j, buf[i][i], (i - 1) * (i - 2) * (j + 1));
				passed = 0;
			}
		}
	}


	// Test stack

	printf("\ntesting stack\n");

	for (i = 0; i < STACKSIZE; i++) {
		stack[i] = i*i - i;
	}

	for (i = 0; i < STACKSIZE; i++) {
		if (stack[i] != i*i - i) {
			printf("pt_test: failed for i=%d\n", i);
		}
	}

	printf("\ndone\n");

	return 0;
}

