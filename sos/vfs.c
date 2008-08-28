#include <stdio.h>
#include <stdlib.h>

#include <l4/types.h>
#include <l4/map.h>
#include <l4/misc.h>
#include <l4/space.h>
#include <l4/thread.h>

#include <sos/sos.h>

#include "console.h"
#include "libsos.h"
#include "pager.h"
#include "vfs.h"

#define verbose 2

// Global record of file descriptors per address space
VNode vnodes[MAX_ADDRSPACES][PROCESS_MAX_FILES];

// All special files (eg. console)
SpecialFile specialFiles = NULL;

void
vfs_init(void) {
	// All vnodes are unallocated
	for (int i = 0; i < MAX_ADDRSPACES; i++) {
		for (int j = 0; j < PROCESS_MAX_FILES; j++) {
			vnodes[i][j].stat.st_type = NULL;
		}
	}

	// One special file is the console
	SpecialFile console = (SpecialFile) malloc(sizeof(struct SpecialFile_t));

	console->file.stat.st_type = ST_SPECIAL;
	console->file.stat.st_fmode = FM_READ | FM_WRITE;
	console->file.stat.st_size = 0;
	console->file.stat.st_ctime = 0;
	console->file.stat.st_atime = 0;

	console->file = (VNode) malloc(sizeof(struct VNode_t));
	console->file->open = console_open;
	console->file->close = console_close;
	console->file->read = console_read;
	console->file->write = console_write;

	console->next = specialFiles;
	specialFiles = console;
}

fildes_t
vfs_open(const char *path, fmode_t mode) {
	dprintf(1, "*** vfs_open: %p (%s) %d\n", path, path, mode);

	VNode *vnode;

	// Check to see if path is one of the special files
	for (SpecialFile sf = specialFiles; sf != NULL; sf = sf->next) {
		if (strcmp(sf->file.path, path) == 0) {
			vnode = sf->file;
			break;
		}
	}

	// Otherwise it must be an actual file
	if (vnode != NULL) {
		dprinf(0, "!!! vfs_open: NFS not supported yet\n");
		return (-1);
	}

	return vnode->open(path, mode);
}

int
vfs_close(fildes_t file) {
	dprintf(1, "*** vfs_close: %d\n", file);
	return 0;
}

int
vfs_read(fildes_t file, char *buf, size_t nbyte) {
	dprintf(1, "*** vfs_read: %d %p %d\n", file, buf, nbyte);
	return 0;
}

int
vfs_write(fildes_t file, const char *buf, size_t nbyte) {
	dprintf(1, "*** vfs_write: %d %p %d\n", file, buf, nbyte);
	return 0;
}

#if 0
/* Add a special file to the list. */
static void addSpecialFile(char *name, int maxReaders) {
	struct GlobalFile_t sf = (GlobalFile) malloc(sizeof(struct GlobalFile_t));
	sf->name = name;
	sf->readers = 0;
	sf->writers = 0;
	sf->maxReaders = maxReaders;
	sf->next = specialFiles;
	specialFiles = sf;
}

/* Initialise the VFS stuff. */

void vfs_init() {
	// The only special file is the console device
	addSpecialFile("console", 1);

	// Every address space has every file descriptor available, except
	// for the standard ones
	for (int i = 0; i < MAX_ADDRSPACES; i++) {
		localFds[i][stdin_fd].mode = FM_READ;
		//localFds[i][stdin_fd].pos = 0;
		localFds[i][stdout_fd].mode = FM_WRITE;
		//localFds[i][stdout_fd].pos = 0;
		localFds[i][stderr_fd].mode = FM_WRITE;
		//localFds[i][stdout_fd].pos = 0;

		for (int j = stderr_fd + 1; j < PROCESS_MAX_FILES; j++) {
			localFds[i].mode = FM_UNALLOCATED;
		}
	}
}
#endif
