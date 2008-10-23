#include <stdio.h>
#include <sos/sos.h>

#define MAX_DEPTH 100

static int naive_fib(int n) {
	if (n <= 0) return 0;
	if (n == 1) return 1;
	return naive_fib(n - 1) + naive_fib(n - 2);
}

int main(int argc, char *argv[]) {
	int id = my_id();

	printf("Stackbomb started: %d\n", id);

	printf("Naive Fibonacci(%d): ", MAX_DEPTH);
	printf("%d\n", naive_fib(MAX_DEPTH));

	printf("Stackbomb finished: %d\n", id);
	return 0;
}

