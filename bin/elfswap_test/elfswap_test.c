#include <assert.h>
#include <sos/sos.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_SLOTS 128
#define SLOTSIZE 192
#define ARRSIZE 128
#define ROUNDS 1024

typedef char test_t;

static char statArray[ARRSIZE];
static int statIndex;

char globArray[ARRSIZE * 2];
int globIndex;

static void assign(char *arr, int from) {
	for (int i = 0; i < ARRSIZE; i++) {
		arr[i] = i + from;
	}
}

static void check(char *arr, int from) {
	for (int i = 0; i < ARRSIZE; i++) {
		assert(arr[i] == i + from);
	}
}

static void thrashHeap(void) {
	test_t **buf = (test_t**) malloc(NUM_SLOTS * sizeof(test_t*));

	for (int i = 0; i < NUM_SLOTS; i++) {
		buf[i] = (test_t*) malloc(SLOTSIZE * sizeof(test_t));

		if (i % 16 == 0) {
			printf("%d... ", i / 16);
			flush(stdout_fd);
		}
	}

	for (int i = 0; i < NUM_SLOTS; i++) {
		if (i % 16 == 0) {
			printf("%d... ", i / 16);
			flush(stdout_fd);
		}

		for (int j = 0; j < SLOTSIZE; j++) {
			assert(buf[i][j] == i*i + j);
		}

		free(buf[i]);
	}

	free(buf);
}

int main(int argc, char *argv[]) {
	statIndex = 1;
	globIndex = statIndex + ROUNDS;

	for (int i = 0; i < ROUNDS; i++) {
		printf("round %d\n", i);
		assign(statArray, i + statIndex);
		assign(globArray, i + globIndex);

		thrashHeap();

		check(statArray, i + statIndex);
		check(globArray, i + globIndex);
	}

	return 0;
}

