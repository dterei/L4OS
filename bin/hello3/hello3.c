#include <stdio.h>
#include <sos/sos.h>

#define SLEEP_MS 3000
#define SIZE 1024

int main(int argc, char *argv[]) {
	char buf[64];
	int *ptr;

	for (int i = 0;; i++) {
		sprintf(buf, ">>> hello from %d, iteration %d\n", my_id(), i);
		kprint(buf);
		ptr = (int*) malloc(SIZE);
		*ptr = i;
		assert(*ptr == i);
		usleep(SLEEP_MS * 1000);
	}

	return 0;
}

