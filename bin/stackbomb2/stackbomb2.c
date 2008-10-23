#include <stdio.h>
#include <sos/sos.h>

#define MAX_DEPTH 4000
#define SPACE_SIZE 512

static int blowstack(int n) {
	if (n <= 0) return 0;
	if (n == 1) return 1;

	// grab some memory
	char space[SPACE_SIZE];
	space[0] = 'a';
	space[SPACE_SIZE - 1] = 'z';

	return 1 + blowstack(n - 1);
}

int main(int argc, char *argv[]) {
	int id = my_id();

	printf("Stackbomb2 started: %d\n", id);

	printf("blowstack(%d): ", MAX_DEPTH);
	printf("%d\n", blowstack(MAX_DEPTH));

	printf("Stackbomb finished: %d\n", id);

	thread_block();
	return 0;
}

