#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <l4/thread.h>
#include <l4/types.h>

#include <sos/ttyout.h>
#include <sos/sos.h>

#include "console.h"
#include "libsos.h"
#include "vfs.h"

#define verbose 2

static int readers = 0;

static VNode findConsole(void) {
	for (SpecialFile sf = specialFiles; sf != NULL; sf = sf->next) {
		if (strcmp(sf->file->path, CONSOLE_PATH) == 0) {
			return sf->file;
		}
	}

	dprintf(0, "!!! couldn't find console\n");
	return NULL;
}

// TODO fix synchronisation issues
fildes_t console_open(const char *path, fmode_t mode) {
	dprintf(1, "*** console_open(%s, %d)\n", path, mode);

	if (readers > 0) {
		// Already have a reader
		return (-1);
	} else {
		readers++;
		int spaceId = L4_SpaceNo(L4_SenderSpace());
		fildes_t fd = findNextFd(spaceId);

		if (fd < 0) {
			return (-1);
		} else {
			vnodes[spaceId][fd] = findConsole();
			return fd;
		}
	}
}

int console_close(fildes_t file) {
	dprintf(1, "*** console_close: %d\n", file);
	return 0;
}

int console_read(fildes_t file, char *buf, size_t nbyte) {
	dprintf(1, "*** console_read: %d %p %d\n", file, buf, nbyte);
	return 0;
}

int console_write(fildes_t file, const char *buf, size_t nbyte) {
	dprintf(1, "*** console_write: %d %p %d\n", file, buf, nbyte);
	return 0;
}

