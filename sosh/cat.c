#include <sos/sos.h>
#include <stdio.h>

#include "cat.h"
#include "sosh.h"

int cat(int argc, char **argv) {
	fildes_t fd;
	char buf[BUF_SIZ];
	int num_read, num_written = 0;

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

	while ((num_read = read(fd, buf, BUF_SIZ)) > 0 ) {
		buf[num_read] = '\0';
		printf("%s", buf);
	}

	close(fd);

	if (num_read == -1) {
		printf( "error on read\n" );
		return 1;
	}

	if (num_written == -1) {
		printf( "error on write\n" );
		return 1;
	}

	return 0;
}

