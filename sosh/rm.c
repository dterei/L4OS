#include <sos/sos.h>
#include <stdio.h>

#include "rm.h"
#include "sosh.h"

int rm(int argc, char **argv) {
	if (argc != 2) {
		printf("usage %s [file]\n", argv[0]);
		return 1;
	}

	int r = fremove(argv[1]);

	if (r < 0) {
		printf("rm(%s) failed: %d\n", argv[1], r);

		if (r == SOS_VFS_NOFILE || r == SOS_VFS_PATHINV || r == SOS_VFS_NOVNODE) {
			printf("file doesn't exist!\n");
		} else if (r == SOS_VFS_PERM) {
			printf("Invalid permissions\n");
		} else if (r == SOS_VFS_NOTIMP) {
			printf("Can't remove this type of file\n");
		} else if (r == SOS_VFS_ERROR) {
			printf("General failure\n");
		} else if (r == SOS_VFS_OPEN) {
			printf("File currently open, can't remove!\n");
		}
	}

	return 0;
}

