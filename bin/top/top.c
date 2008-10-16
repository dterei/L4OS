#include <sos/sos.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
	printf("Mem: %d\n", memuse());
	printf("Phys: %d\n", physuse());
	printf("Swap: %d\n", swapuse());
	return 0;
}

