#include <stdio.h>
#include <sos/sos.h>

#define PROGRAM "pt_test2"

int main(int argc, char *argv[]) {
	char buf[BUFSIZ];

	for (;;) {
		pid_t id = process_create(PROGRAM);
		sprintf(buf, "spawned %d\n", id);
		kprint(buf);
	}

	return 0;
}

