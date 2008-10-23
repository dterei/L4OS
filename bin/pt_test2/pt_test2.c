#include <assert.h>
#include <sos/sos.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_SLOTS 128
#define SLOTSIZE 192
#define STACKSIZE 4096

typedef int test_t;

int main(int argc, char *argv[]) {
	int i, j, passed;
	test_t **buf;
	test_t stack[STACKSIZE];

	// Test heap

	buf = (test_t**) malloc(NUM_SLOTS * sizeof(test_t*));

	for (i = 0; i < NUM_SLOTS; i++) {
		buf[i] = (test_t*) malloc(SLOTSIZE * sizeof(test_t));
	}

	for (i = 0; i < NUM_SLOTS; i++) {
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
	}

	// Try a different touch value

	for (i = 0; i < NUM_SLOTS; i++) {
		for (j = 0; j < SLOTSIZE; j++) {
			buf[i][j] = (i - 1) * (i - 2) * (j + 1);
		}
	}

	// And again

	for (i = 0; i < NUM_SLOTS; i++) {
		passed = 1;

		for (j = 0; j < SLOTSIZE; j++) {
			if (buf[i][j] != (i - 1) * (i - 2) * (j + 1)) {
				printf("pt_test: failed for i=%d j=%d (%d vs %d)\n",
						i, j, buf[i][i], (i - 1) * (i - 2) * (j + 1));
				passed = 0;
			}
		}
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

