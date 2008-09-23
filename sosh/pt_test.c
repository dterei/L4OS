#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <sos/sos.h>

#include "pt_test.h"
#include "sosh.h"

#define NUM_SLOTS 4096
#define SLOTSIZE 196
#define STACKSIZE 512

typedef int test_t;

int pt_test(int argc, char **argv) {
	int i, j, passed;
	test_t **buf;
	test_t stack [STACKSIZE];

	// Test heap

	buf = (test_t**) malloc(NUM_SLOTS * sizeof(test_t*));

	for (i = 0; i < NUM_SLOTS; i++) {
		buf[i] = (test_t*) malloc(SLOTSIZE * sizeof(test_t));

		for (j = 0; j < SLOTSIZE; j++) {
			buf[i][j] = i*i + j;
		}
	}

	// Do in two rounds in case memory is going crazy

	for (i = 0; i < NUM_SLOTS; i++) {
		passed = 1;

		for (j = 0; j < SLOTSIZE; j++) {
			if (buf[i][j] != i*i + j) {
				printf("pt_test: failed for i=%d j=%d (%d vs %d)\n",
						i, j, buf[i][i], i*i + j);
				passed = 0;
			}
		}

		//if (passed) printf("test passed for i=%d\n", i);
	}

	// Test stack

	for (i = 0; i < STACKSIZE; i++) {
		stack[i] = i*i - i;
	}

	for (i = 0; i < STACKSIZE; i++) {
		if (stack[i] != i*i - i) {
			printf("pt_test: failed for i=%d\n", i);
		}
	}

	return 0;
}

