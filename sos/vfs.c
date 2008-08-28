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

	// Init console files
	specialFiles = console_init(specialFiles);
}

fildes_t
vfs_open(L4_ThreadId_t tid, const char *path, fmode_t mode) {
	dprintf(1, "*** vfs_open: %p (%s) %d\n", path, path, mode);

	VNode vnode = NULL;

	// Check to see if path is one of the special files
	for (SpecialFile sf = specialFiles; sf != NULL; sf = sf->next) {
		dprintf(0, "*** Special file: %s ***\n", sf->file->path);
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
		return vnode->open(tid, vnode, path, mode);
	}
}

int
vfs_close(L4_ThreadId_t tid, fildes_t file) {
	dprintf(1, "*** vfs_close: %d\n", file);

	VNode vnode = vnodes[L4_SpaceNo(L4_SenderSpace())][file];
	if (vnode == NULL)
		return (-1);

	return vnode->close(tid, vnode, file);
}

int
vfs_read(L4_ThreadId_t tid, fildes_t file, char *buf, size_t nbyte) {
	dprintf(1, "*** vfs_read: %d %p %d\n", file, buf, nbyte);

	VNode vnode = vnodes[L4_SpaceNo(L4_SenderSpace())][file];
	if (vnode == NULL)
		return (-1);

	return vnode->read(tid, vnode, file, buf, nbyte);
}

int
vfs_write(L4_ThreadId_t tid, fildes_t file, const char *buf, size_t nbyte) {
	dprintf(1, "*** vfs_write: %d %p %d\n", file, buf, nbyte);

	VNode vnode = vnodes[L4_SpaceNo(L4_SenderSpace())][file];
	if (vnode == NULL)
		return (-1);

	return vnode->write(tid, vnode, file, buf, nbyte);
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

