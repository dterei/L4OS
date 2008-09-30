#include <sos/sos.h>
#include <stdio.h>
#include <string.h>

#include "ls.h"
#include "sosh.h"

static stat_t sbuf;

static void prstat(const char *name) {
	printf("%c%c%c%c 0x%06x 0x%lx 0x%06lx %s\n",
			sbuf.st_type == ST_SPECIAL  ? 's' : '-',
			sbuf.st_fmode & FM_READ     ? 'r' : '-',
			sbuf.st_fmode & FM_WRITE    ? 'w' : '-',
			sbuf.st_fmode & FM_EXEC     ? 'x' : '-',
			sbuf.st_size, sbuf.st_ctime, sbuf.st_atime, name);
}

int ls(int argc, char **argv) {
	int i, r, a = 0, statp = 1;
	char buf[BUF_SIZ];

	if (argc > 3) {
		printf("usage: %s [-a] [file]\n", argv[0]);
		return 1;
	}


	if (argc >= 2) {
		int cmp = strcmp(argv[1], "-a");
		if (verbose > 1) {
			printf("%s\n", argv[1]);
			printf("cmp: %d\n", cmp);
		}
		if (cmp == 0) {
			a = 1;
			statp = 2;
		}
	}
		
	if (verbose > 1) {
		printf("a = %d, statp = %d\n", a, statp);
	}

	if (argc > statp) {
		r = stat(argv[statp], &sbuf);

		if (r < 0) {
			printf("stat(%s) failed: %d\n", argv[statp], r);
			return 0;
		}

		prstat(argv[statp]);
		return 0;
	}

	for (i = 0;; i++) {
		r = getdirent(i, buf, BUF_SIZ);

		if (r < 0) {
			printf("dirent(%d) failed: %d\n", i, r);
			break;
		} else if (!r) {
			break;
		}

#if 0
		printf("dirent(%d): \"%s\"\n", i, buf);
#endif

		if (a == 1 || strncmp(buf, ".", 1) != 0) {
			r = stat(buf, &sbuf);
		} else {
			continue;
		}

		if (r < 0) {
			printf("stat(%s) failed: %d\n", buf, r);
			break;
		}

		prstat(buf);
	}

	return 0;
}

