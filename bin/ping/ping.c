#include <stdio.h>
#include <sos/sos.h>

#define SLEEP_MS 10000

int main(int argc, char *argv[]) {
	for (int i = 0;; i++) {
		kprint("------------------------------------\n");
		kprint("ping\n");
		kprint("------------------------------------\n");
		usleep(SLEEP_MS * 1000);
	}

	return 0;
}

