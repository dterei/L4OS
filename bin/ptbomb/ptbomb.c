#include <stdio.h>
#include <sos/sos.h>

#define PROGRAM "pt_test2"

int main(int argc, char *argv[]) {
	for (;;) {
		pid_t id = process_create(PROGRAM);
		printf("spawned %d\n", id);
	}

	return 0;
}

