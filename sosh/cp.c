#include <sos/sos.h>
#include <stdio.h>

#include "cp.h"
#include "sosh.h"

int cp(int argc, char **argv) {
	fildes_t fd, fd_out;
	char *file1, *file2;
	char buf[BUF_SIZ];
	int num_read, num_written = 0;

	if (argc != 3) {
		printf("Usage: cp from to %d\n", argc);
		return 1;
	}

	file1 = argv[1];
	file2 = argv[2];

	fd = open(file1, FM_READ);
	if (fd < 0) {
		printf("%s cannot be opened\n", file1);
		return 1;
	}

	fd_out = open(file2, FM_WRITE);
	if (fd_out < 0) {
		printf("%s cannot be opened\n", file2);
		close(fd);
		return 1;
	}

	while ((num_read = read( fd, buf, BUF_SIZ) ) > 0) {
		num_written = write(fd_out, buf, num_read);
	}

	if (num_read == -1 || num_written == -1) {
		close(fd);
		close(fd_out);
		printf( "error on cp\n" );
		return 1;
	}

	close(fd);
	close(fd_out);
	return 0;
}

