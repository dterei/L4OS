#include <sos/globals.h>
#include <sos/sos.h>
#include <stdio.h>

#define LOCK_FILE ".lockfile"

int main(int argc, char **argv) {
	char *a[2];
	argv = a;
	a[0] = "lockfile";
	a[1] = LOCK_FILE;
	argc = 2;

	if (argc != 2) {
		printf("Usage: %s <file>\n", argv[0]);
		return 1;
	}

	fildes_t fd = open_lock(argv[1], FM_READ, 1, 0);
	if (fd < 0) {
		printf("%s cannot be opened: %s\n", argv[1], sos_error_msg(fd));
		return 1;
	}

	thread_block();

	close(fd);

	return 0;
}

