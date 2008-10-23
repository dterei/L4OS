#include <stdio.h>
#include <sos/sos.h>

#define PROGRAM "forkbomb2"

int main(int argc, char *argv[]) {
	int id = my_id();

	printf("+%d ", id);
	process_create(PROGRAM);
	process_create(PROGRAM);
	printf("-%d ", id);
	return 0;
}

