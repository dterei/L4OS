#include <sos/sos.h>

#include "kecho.h"
#include "sosh.h"

int kecho(int argc, char **argv) {
	for (int i = 1; i < argc; i++) {
		if (i > 1) kprint(" ");
		kprint(argv[i]);
	}

	kprint("\n");
	return 1;
}

