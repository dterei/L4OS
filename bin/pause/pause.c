#include <sos/sos.h>
#include <stdio.h>

#define PAUSE_MSG "\nPress <Enter> to continue..."

int main(int argc, char *argv[]) {
	printf(PAUSE_MSG);

	char buf[1];
	int done = 0;

	fildes_t in = open("console", FM_READ);
	if (in < 0) {
		printf("Error opening console!\n");
		printf("%s\n", sos_error_msg(in));
		return 1;
	}

	while (!done) {
		int r = read(in, buf, 1);
		if (r == SOS_VFS_EOF) {
			break;
		} else if (r > 0 && buf[0] == '\n') {
			break;
		}
		printf(PAUSE_MSG);
	}

	return 0;
}

