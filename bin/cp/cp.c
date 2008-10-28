#include <sos/globals.h>
#include <sos/sos.h>
#include <stdio.h>

int main(int argc, char **argv) {
	fildes_t fd, fd_out;
	char *file1, *file2;
	char buf[IO_MAX_BUFFER];
	int num_read, num_written = 0;

	if (argc != 3) {
		printf("Usage: cp from to %d\n", argc);
		return 1;
	}

	file1 = argv[1];
	file2 = argv[2];

	fd = open(file1, FM_READ);
	if (fd < 0) {
		printf("%s cannot be opened: %s\n", file1, sos_error_msg(fd));
		return 1;
	}

	fd_out = open(file2, FM_WRITE);
	if (fd_out < 0) {
		printf("%s cannot be opened: %s\n", file2, sos_error_msg(fd));
		close(fd);
		return 1;
	}

	while ((num_read = read( fd, buf, IO_MAX_BUFFER) ) > 0) {
		num_written = write(fd_out, buf, num_read);
	}

	if ((num_read != SOS_VFS_EOF && num_read < 0) || num_written < 0) {
		close(fd);
		close(fd_out);
		printf("error on cp: %s\n", sos_error_msg(fd));
		return 1;
	}

	close(fd);
	close(fd_out);
	return 0;
}

