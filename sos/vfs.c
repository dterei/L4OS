#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <l4/types.h>
#include <l4/map.h>
#include <l4/misc.h>
#include <l4/space.h>
#include <l4/thread.h>

#include <sos/sos.h>

#include "vfs.h"

#include "console.h"
#include "nfsfs.h"

#include "libsos.h"
#include "pager.h"

#define verbose 1

// Global open vnodes list
VNode GlobalVNodes;

// Per process open file table
VFile_t openfiles[MAX_ADDRSPACES][PROCESS_MAX_FILES];

// All special files (eg. console)
VNode specialFiles = NULL;


void
vfs_init(void) {
	// All vnodes are unallocated
	for (int i = 0; i < MAX_ADDRSPACES; i++) {
		for (int j = 0; j < PROCESS_MAX_FILES; j++) {
			openfiles[i][j].vnode = NULL;
			openfiles[i][j].fmode = 0;
			openfiles[i][j].fp = 0;
		}
	}

	// Init console files
	specialFiles = console_init(specialFiles);
	// Init NFS
	nfsfs_init();
}

fildes_t
vfs_open(L4_ThreadId_t tid, const char *path, fmode_t mode) {
	dprintf(1, "*** vfs_open: %p (%s) %d\n", path, path, mode);

	VNode vnode = NULL;

	// Check to see if path is one of the special files
	for (vnode = specialFiles; vnode != NULL; vnode = vnode->next) {
		dprintf(2, "*** Special file: %s ***\n", vnode->path);
		if (strcmp(vnode->path, path) == 0) break;
	}

	// Not special file, assume NFS, we dont support mount
	// points so no point getting fancy to support multiple
	// filesystems more transparently.
	if (vnode == NULL) {
		// TODO: NFS Open
		dprintf(0, "!!! vfs_open: NFS not supported yet\n");
		return (-1);
	}
	
	// Have vnode now so open
	fildes_t rval = vnode->open(tid, vnode, path, mode);

	// open failed
	if (rval < 0) {
		dprintf(2, "*** vfs_open: can't open file: error code %d\n", rval);
		return (-1);
	}

	// store file in per process table
	openfiles[getCurrentProcNum()][rval].vnode = vnode;
	openfiles[getCurrentProcNum()][rval].fmode = mode;
	openfiles[getCurrentProcNum()][rval].fp = 0;

	// update global vnode list
	VNode oldhead = GlobalVNodes;
	GlobalVNodes = vnode;
	vnode->next = oldhead;
	vnode->previous = NULL;
	oldhead->previous = vnode;

	// update vnode refcount
	vnode->refcount++;

	return rval;
}

int
vfs_close(L4_ThreadId_t tid, fildes_t file) {
	dprintf(1, "*** vfs_close: %d\n", file);

	// get file
	VFile_t *vf = &openfiles[getCurrentProcNum()][file];

	// get vnode
	VNode vnode =vf->vnode;
	if (vnode == NULL) {
		dprintf(2, "*** vfs_close: invalid file handler: %d\n", file);
		return (-1);
	}

	// vnode close function is responsible for freeing the node if appropriate
	// so don't access after a close without checking its not null
	// try closing vnode;
	int r = vnode->close(tid, vnode, file, vf->fmode);

	// clean up book keeping
	if (r == 0) {
		// close file table entry
		vf->vnode = NULL;
		vf->fmode = 0;
		vf->fp = 0;

		// close global vnode entry if returned vnode is null
		if (vnode == NULL) {
			VNode previous = vnode->previous;
			VNode next = vnode->next;
			previous->next = next;
			next->previous = previous;
		} else {
			vnode->refcount--;
		}
	}

	return r;
}

void
vfs_read(L4_ThreadId_t tid, fildes_t file, char *buf, size_t nbyte, int *rval) {
	dprintf(1, "*** vfs_read: %d %p %d\n", file, buf, nbyte);

	// get file
	VFile_t *vf = &openfiles[getCurrentProcNum()][file];

	// get vnode
	VNode vnode = vf->vnode;
	if (vnode == NULL) {
		dprintf(2, "*** vfs_read: invalid file handler: %d\n", file);
		*rval = (-1);
		return;
	}

	// check permissions
	if (!(vf->fmode & FM_READ)) {
		dprintf(2, "*** vfs_read: invalid read permissions for file: %d, %d\n",
				file, vf->fmode);
		*rval = (-1);
		return;
	}

	vnode->read(tid, vnode, file, vf->fp, buf, nbyte, rval);
}

void
vfs_write(L4_ThreadId_t tid, fildes_t file, const char *buf, size_t nbyte, int *rval) {
	dprintf(1, "*** vfs_write: %d %p %d\n", file, buf, nbyte);

	// get file
	VFile_t *vf = &openfiles[getCurrentProcNum()][file];

	// get vnode
	VNode vnode = vf->vnode;
	if (vnode == NULL) {
		dprintf(2, "*** vfs_write: invalid file handler: %d\n", file);
		*rval = (-1);
		return;
	}

	// check permissions
	if (!(vf->fmode & FM_WRITE)) {
		dprintf(2, "*** vfs_write: invalid read permissions for file: %d, %d\n",
				file, vf->fmode);
		*rval = (-1);
		return;
	}

	vnode->write(tid, vnode, file, vf->fp, buf, nbyte, rval);
}

fildes_t
findNextFd(int spaceId) {
	// TODO: Should we be reserving these really?
	// 0, 1, 2 reserved for the standard file descriptors
	for (int i = 3; i < MAX_ADDRSPACES; i++) {
		if (openfiles[spaceId][i].vnode == NULL) return i;
	}

	// Too many open files.
	return (-1);
}

