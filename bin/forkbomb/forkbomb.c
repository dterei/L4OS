#include <stdio.h>
#include <sos/sos.h>

#define PROGRAM "forkbomb"

int main(int argc, char *argv[]) {
	int id = my_id();

	printf("Forkbomb spawned: %d\n", id);
	int child1 = process_create(PROGRAM);
	//int child2 = process_create(PROGRAM);

	if (child1 >= 0) {
		printf("Forkbomb spawning new: %d\n", child1);
		process_wait(child1);
		//process_wait(child2);
	}

	printf("Forkbomb dying: %d\n", id);
	return 0;
}

