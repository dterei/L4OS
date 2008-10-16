#include <sos/sos.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
	if (argc != 2) {
		printf("usage %s [file]\n", argv[0]);
		return 1;
	}

	int r = fremove(argv[1]);

	if (r < 0) {
		printf("rm(%s) failed: %d\n", argv[1], r);
		printf("Can't remove file, %s\n", sos_error_msg(r));
	}

	return 0;
}

