#include <sos/sos.h>
#include <stdio.h>

#include "cat.h"
#include "sosh.h"

#define cat_verbose 1

int cat(int argc, char **argv) {
	fildes_t fd;
	char buf[BUF_SIZ];
	int num_read, num_written = 0, read_tot = 0, write_tot = 0;
	int ret = 0;

	if (argc != 2) {
		printf("Usage: cat filename\n");
		return 1;
	}

	fd = open(argv[1], FM_READ);
	if (fd < 0) {
		printf("%s cannot be opened\n", argv[1]);
		return 1;
	}

	//printf("<%s>\n", argv[1]);

	while ((num_read = read(fd, buf, BUF_SIZ - 1)) > 0 ) {
		buf[num_read] = '\0';
		// use write instead of printf as printf has some bugs with
		// printing weird chars
		if ((num_written = write(stdout_fd, buf, num_read)) < 0) {
			break;
		}
		read_tot += num_read;
		write_tot += num_written;
	}

	close(fd);

	if (num_read == -1) {
		printf( "error on read\n" );
		kprint( "error on read\n" );
		ret = 1;
	}

	if (num_written == -1) {
		printf( "error on write\n" );
		kprint( "error on write\n" );
		ret = 1;
	}

	if (cat_verbose > 1) {
		sprintf(buf, "Total Read: %d, Total Write: %d\n", read_tot, write_tot);
		printf("%s", buf);
		kprint(buf);
	}

	return ret;
}

