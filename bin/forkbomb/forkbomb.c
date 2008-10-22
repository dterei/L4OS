#include <stdio.h>
#include <sos/sos.h>

#define PROGRAM "forkbomb"

int main(int argc, char *argv[]) {
	int id = my_id();

	printf("Forkbomb spawned: %d\n", id);
	int child1 = process_create(PROGRAM);
	int child2 = process_create(PROGRAM);

	if (child1 >= 0 && (id % 2 == 1 || child2 < 0)) {
		printf("Forkbomb spawning new child1: %d\n", child1);
		process_wait(child1);
	} else if (child2 >= 0) {
		printf("Forkbomb spawning new child2: %d\n", child2);
		process_wait(child2);
	}

	printf("Forkbomb dying: %d\n", id);
	return 0;
}

