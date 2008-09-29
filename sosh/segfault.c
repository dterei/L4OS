#include <sos/sos.h>
#include <stdio.h>

#include "segfault.h"
#include "sosh.h"

int segfault(int argc, char **argv) {
	int *null = NULL;
	return *null;
}

