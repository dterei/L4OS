#include <sos/sos.h>
#include <stdio.h>

#include "exec.h"
#include "sosh.h"

static fildes_t in;

int exec(int argc, char **argv) {
	pid_t pid;
	int r;
	int bg = 0;

	if (argc < 2 || (argc > 2 && argv[2][0] != '&')) {
		printf("Usage: exec filename [&]\n");
		return 1;
	}

	if ((argc > 2) && (argv[2][0] == '&')) {
		bg = 1;
	}

	if (bg == 0) {
		r = close(in);
		assert(r == 0);
	}

	pid = process_create(argv[1]);

	if (pid >= 0) {
		printf("Child pid=%d\n", pid);
		if (bg == 0) {
			process_wait(pid);
		}
	} else {
		printf("Failed!\n");
		return 1;
	}

	if (bg == 0) {
		in = open("console", FM_READ);
		assert(in>=0);
	}

	return 0;
}

