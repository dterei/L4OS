// build with "gcc -std=c99 -Wall -O -g listtest.c list.c -o test"

#include <assert.h>
#include <stdio.h>

#include "list.h"

#define TEST printf("TEST %d\n", __LINE__);

#define RANGE 20

static int deleteAll(void *contents, void *data) {
	return 1;
}

static int deleteNone(void *contents, void *data) {
	return 0;
}

static int deleteOdd(void *contents, void *data) {
	return ((int) contents % 2 == 1);
}

static int deleteEven(void *contents, void *data) {
	return ((int) contents % 2 == 0);
}

static void *sum(void *contents, void *data) {
	return (void*) ((int) data) + ((int) contents);
}

static void *sub(void *contents, void *data) {
	return (void*) ((int) data) - ((int) contents);
}

static void printInt(void *contents, void *data) {
	(void) printInt;
	int *index = (int*) data;

	printf("%d: %d\n", *index, (int) contents);
	(*index)++;
}

int main(int argc, char *argv[]) {
	TEST {
		List *list = list_empty();

		for (int i = 0; i < RANGE; i++) {
			list_push(list, (void*) i);
		}

		for (int i = 0; i < RANGE; i++) {
			assert(list_unshift(list) == (void*) i);
		}

		assert(list_null(list));
		assert(list_destroy(list) == 0);
	}

	TEST {
		List *list = list_empty();

		for (int i = 0; i < RANGE; i++) {
			list_push(list, (void*) i);
		}

		for (int i = RANGE - 1; i >= 0; i--) {
			assert(list_pop(list) == (void*) i);
		}

		assert(list_null(list));
		assert(list_destroy(list) == 0);
	}

	TEST {
		List *list = list_empty();

		for (int i = RANGE - 1; i >= 0; i--) {
			list_shift(list, (void*) i);
		}

		for (int i = 0; i < RANGE; i++) {
			assert(list_unshift(list) == (void*) i);
		}

		assert(list_null(list));
		assert(list_destroy(list) == 0);
	}

	TEST {
		List *list = list_empty();

		for (int i = RANGE - 1; i >= 0; i--) {
			list_shift(list, (void*) i);
		}

		for (int i = RANGE - 1; i >= 0; i--) {
			assert(list_pop(list) == (void*) i);
		}

		assert(list_null(list));
		assert(list_destroy(list) == 0);
	}

	TEST {
		List *list = list_empty();

		for (int i = 0; i < RANGE; i++) {
			list_push(list, (void*) i);
		}

		for (int i = 0; i < RANGE; i++) {
			assert(list_unshift(list) == (void*) i);
		}

		assert(list_null(list));

		for (int i = RANGE - 1; i >= 0; i--) {
			list_shift(list, (void*) i);
		}

		for (int i = RANGE - 1; i >= 0; i--) {
			assert(list_pop(list) == (void*) i);
		}

		assert(list_null(list));
		assert(list_destroy(list) == 0);
	}

	TEST {
		List *list = list_empty();

		for (int i = 0; i < RANGE; i++) {
			list_push(list, (void*) i);
		}

		list_delete(list, deleteNone, NULL);

		for (int i = 0; i < RANGE; i++) {
			assert(list_unshift(list) == (void*) i);
		}

		assert(list_null(list));
		assert(list_destroy(list) == 0);
	}

	TEST {
		List *list = list_empty();

		for (int i = 0; i < RANGE; i++) {
			list_push(list, (void*) i);
		}

		list_delete(list, deleteEven, NULL);

		for (int i = 1; i < RANGE; i += 2) {
			assert(list_unshift(list) == (void*) i);
		}

		assert(list_null(list));
		assert(list_destroy(list) == 0);
	}

	TEST {
		List *list = list_empty();

		for (int i = 0; i < RANGE; i++) {
			list_push(list, (void*) i);
		}

		list_delete(list, deleteOdd, NULL);

		for (int i = 0; i < RANGE; i += 2) {
			assert(list_unshift(list) == (void*) i);
		}

		assert(list_null(list));
		assert(list_destroy(list) == 0);
	}

	TEST {
		List *list = list_empty();

		for (int i = 0; i < RANGE; i++) {
			list_push(list, (void*) i);
		}

		list_delete_first(list, deleteNone, NULL);

		for (int i = 0; i < RANGE; i++) {
			assert(list_unshift(list) == (void*) i);
		}

		assert(list_null(list));
		assert(list_destroy(list) == 0);
	}

	TEST {
		List *list = list_empty();

		for (int i = 0; i < RANGE; i++) {
			list_push(list, (void*) i);
		}

		list_delete_first(list, deleteEven, NULL);

		for (int i = 1; i < RANGE; i++) {
			assert(list_unshift(list) == (void*) i);
		}

		assert(list_null(list));
		assert(list_destroy(list) == 0);
	}

	TEST {
		List *list = list_empty();

		for (int i = 0; i < RANGE; i++) {
			list_push(list, (void*) i);
		}

		list_delete_first(list, deleteOdd, NULL);

		for (int i = 0; i < RANGE; i ++) {
			if (i == 1) continue;
			assert(list_unshift(list) == (void*) i);
		}

		assert(list_null(list));
		assert(list_destroy(list) == 0);
	}

	TEST {
		List *list = list_empty();

		for (int i = 0; i < RANGE; i++) {
			list_push(list, (void*) i);
		}

		list_delete(list, deleteAll, NULL);

		assert(list_null(list));
		assert(list_destroy(list) == 0);
	}

	TEST {
		List *list = list_empty();

		for (int i = 0; i < RANGE; i++) {
			list_push(list, (void*) i);
		}

		list_delete_first(list, deleteAll, NULL);

		for (int i = 1; i < RANGE; i ++) {
			assert(list_unshift(list) == (void*) i);
		}

		assert(list_null(list));
		assert(list_destroy(list) == 0);
	}

	TEST {
		List *list = list_empty();

		list_push(list, (void*) 10);
		list_delete_first(list, deleteAll, NULL);
		assert(list_null(list));

		list_push(list, (void*) 11);
		list_push(list, (void*) 12);
		list_delete_first(list, deleteAll, NULL);
		assert(list_unshift(list) == (void*) 12);
		assert(list_null(list));

		list_push(list, (void*) 11);
		list_push(list, (void*) 12);
		list_delete_first(list, deleteEven, NULL);
		assert(list_unshift(list) == (void*) 11);
		assert(list_null(list));

		assert(list_destroy(list) == 0);
	}

	TEST {
		List *list = list_empty();

		for (int i = 0; i < RANGE; i++) {
			list_push(list, (void*) i);
		}

		int tmp1 = (int) list_reduce(list, sum, 0);
		int tmp2 = (RANGE * (RANGE - 1)) / 2;

		assert(tmp1 == tmp2);

		tmp1 = (int) list_reduce(list, sub, 0);
		tmp2 = -tmp2;

		assert(tmp1 == tmp2);
		assert(list_destroy(list) == RANGE);
	}

	return 0;
}

