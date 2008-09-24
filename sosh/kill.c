#include <sos/sos.h>
#include <stdio.h>
#include <stdlib.h>

#include "kill.h"

int kill(int argc, char **argv) {
	if (argc < 2) {
		printf("usage: %s pid\n", argv[0]);
	} else {
		process_delete(atoi(argv[1]));
	}

	return 0;
}

