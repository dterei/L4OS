#include <sos/ttyout.h>
#include <sos/sos.h>

#include "console.h"

#define verbose 2

static int readers = 0;

static VNode findConsole(void) {
	for (SpecialFile sf = specialFiles; sf != NULL; sf = sf->next) {
		if (strcmp(sf->file->path, "console") == 0) {
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
		int spaceId = L4_SpaceId(L4_SenderSpace());
		filedes_t fd = findNextFd(spaceId);
		fds[spaceId][fd] = findConsole();
	}
}

int console_close(fildes_t file) {
	;
}

int console_read(fildes_t file, char *buf, size_t nbyte) {
	;
}

int console_write(fildes_t file, const char *buf, size_t nbyte) {
	;
}

