#include <stdio.h>
#include <sos/sos.h>

#define SLEEP_MS 3000

int main(int argc, char *argv[]) {
	char buf[64];

	for (int i = 0;; i++) {
		sprintf(buf, ">>> hello from %d, iteration %d\n", my_id(), i);
		kprint(buf);
		usleep(SLEEP_MS * 1000);
	}

	return 0;
}

