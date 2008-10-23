#include <stdio.h>
#include <sos/sos.h>

#define PROGRAM "forkbomb3"

int main(int argc, char *argv[]) {
	process_create(PROGRAM);
	process_create(PROGRAM);
	return 0;
}

