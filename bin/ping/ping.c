#include <stdio.h>
#include <sos/sos.h>

#define SLEEP_MS 10000

int main(int argc, char *argv[]) {
	char buf[32];

	for (int i = 0;; i++) {
		//kprint("------------------------------------\n");
		sprintf(buf, "ping: up for %llu ms\n", uptime());
		kprint(buf);
		//kprint("------------------------------------\n");
		usleep(SLEEP_MS * 1000);
	}

	return 0;
}

