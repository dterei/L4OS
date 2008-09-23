#include <l4/types.h>
#include <sos/sos.h>
#include <stdio.h>

#include "pid.h"

int pid(int argc, char **argv) {
	printf("%u\n", my_id());
	return 0;
}

