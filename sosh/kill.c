#include <sos/sos.h>
#include <stdio.h>
#include <stdlib.h>

#include "kill.h"

int kill(int argc, char **argv) {
	if (argc < 2) {
		printf("usage: %s pid\n", argv[0]);
	} else if (process_delete(atoi(argv[1])) == (-1)) {
		printf("%s %s failed (invalid process)\n", argv[0], argv[1]);
	} else {
		printf("successfully killed process %s\n", argv[1]);
	}

	return 0;
}

