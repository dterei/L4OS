#include <sos/sos.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
	printf("Free: %d\n", memfree());
	printf("Phys: %d\n", physuse());
	printf("Swap: %d\n", swapuse());
	return 0;
}

