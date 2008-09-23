#include <sos/sos.h>

#include "sleep.h"
#include "sosh.h"

int sleep(int argc, char **argv) {
	if (argc < 2) {
		printf("usage: %s msec\n", argv[0]);
		return 1;
	}

	int msec = atoi(argv[1]);
	usleep(msec * 1000);
	return 0;
}

