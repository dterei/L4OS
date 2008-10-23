#include <stdio.h>
#include <sos/sos.h>

#define PROGRAM "pt_test2"
#define SLEEP_MS 1000

int main(int argc, char *argv[]) {
	for (;;) {
		pid_t id = process_create(PROGRAM);
		printf("spawned %d\n", id);
		usleep(SLEEP_MS * 1000);
	}

	return 0;
}

