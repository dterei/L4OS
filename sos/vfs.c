#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
			vnodes[i][j] = NULL;
		}
	}

	// One special file is the console
	SpecialFile console = (SpecialFile) malloc(sizeof(struct SpecialFile_t));
	console->file = (VNode) malloc(sizeof(struct VNode_t));

	console->file->path = CONSOLE_PATH;

	console->file->stat.st_type = ST_SPECIAL;
	console->file->stat.st_fmode = FM_READ | FM_WRITE;
	console->file->stat.st_size = 0;
	console->file->stat.st_ctime = 0;
	console->file->stat.st_atime = 0;

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

	VNode vnode = NULL;

	// Check to see if path is one of the special files
	for (SpecialFile sf = specialFiles; sf != NULL; sf = sf->next) {
		if (strcmp(sf->file->path, path) == 0) {
			vnode = sf->file;
			break;
		}
	}

	if (vnode == NULL) {
		// Must be NFS
		dprintf(0, "!!! vfs_open: NFS not supported yet\n");
		return (-1);
	} else {
		// Was special file
		return vnode->open(path, mode);
	}
}

int
vfs_close(fildes_t file) {
	dprintf(1, "*** vfs_close: %d\n", file);
	return vnodes[L4_SpaceNo(L4_SenderSpace())][file]->close(file);
}

int
vfs_read(fildes_t file, char *buf, size_t nbyte) {
	dprintf(1, "*** vfs_read: %d %p %d\n", file, buf, nbyte);
	return vnodes[L4_SpaceNo(L4_SenderSpace())][file]->read(file, buf, nbyte);
}

int
vfs_write(fildes_t file, const char *buf, size_t nbyte) {
	dprintf(1, "*** vfs_write: %d %p %d\n", file, buf, nbyte);
	return vnodes[L4_SpaceNo(L4_SenderSpace())][file]->write(file, buf, nbyte);
}

fildes_t
findNextFd(int spaceId) {
	// 0, 1, 2 reserved for the standard file descriptors
	for (int i = 3; i < MAX_ADDRSPACES; i++) {
		if (vnodes[spaceId][i] == NULL) return i;
	}

	// Too many open files.
	return (-1);
}
