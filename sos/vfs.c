#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <l4/types.h>
#include <l4/map.h>
#include <l4/misc.h>
#include <l4/space.h>
#include <l4/thread.h>

#include <sos/sos.h>

#include "l4.h"

#include "vfs.h"

#include "console.h"
#include "nfsfs.h"

#include "libsos.h"
#include "pager.h"

#define verbose 2

// Global open vnodes list
VNode GlobalVNodes;

// Per process open file table
VFile_t openfiles[MAX_ADDRSPACES][PROCESS_MAX_FILES];

// All special files (eg. console)
VNode specialFiles = NULL;


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

void
vfs_open(L4_ThreadId_t tid, const char *path, fmode_t mode, int *rval) {
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
		dprintf(2, "*** vfs_open: try to open file with nfs: %s\n", path);
		nfsfs_open(tid, vnode, path, mode, rval, vfs_open_done);
		return;
	}
	
	// Have vnode now so open
	vnode->open(tid, vnode, path, mode, rval, vfs_open_done);
}

void
vfs_open_done(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode, int *rval) {
	dprintf(1, "*** vfs_open_done: %p (%s) %d\n", path, path, mode);

	// open failed
	if (*rval < 0) {
		dprintf(2, "*** vfs_open: can't open file: error code %d\n", *rval);
		return;
	}

	// store file in per process table
	openfiles[getCurrentProcNum()][*rval].vnode = self;
	openfiles[getCurrentProcNum()][*rval].fmode = mode;
	openfiles[getCurrentProcNum()][*rval].fp = 0;

	// update global vnode list
	VNode oldhead = GlobalVNodes;
	GlobalVNodes = self;
	self->next = oldhead;
	self->previous = NULL;
	oldhead->previous = self;

	// update vnode refcount
	self->refcount++;
}

void
vfs_close(L4_ThreadId_t tid, fildes_t file, int *rval) {
	dprintf(1, "*** vfs_close: %d\n", file);

	// get file
	VFile_t *vf = &openfiles[getCurrentProcNum()][file];

	// get vnode
	VNode vnode =vf->vnode;
	if (vnode == NULL) {
		dprintf(2, "*** vfs_close: invalid file handler: %d\n", file);
		*rval = (-1);
		return;
	}

	// vnode close function is responsible for freeing the node if appropriate
	// so don't access after a close without checking its not null
	// try closing vnode;
	vnode->close(tid, vnode, file, vf->fmode, rval, vfs_close_done);
}

void
vfs_close_done(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode, int *rval) {
	dprintf(1, "*** vfs_close_done: %d\n", file);

	// get file & vnode
	VFile_t *vf = &openfiles[getCurrentProcNum()][file];
	VNode vnode =vf->vnode;

	// clean up book keeping
	if (*rval == 0) {
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

void
vfs_getdirent(L4_ThreadId_t tid, int pos, char *name, size_t nbyte, int *rval) {
	dprintf(1, "*** vfs_getdirent: %d, %s, %d\n", pos, name, nbyte);
	msgClear();
	L4_Reply(tid);
}


void
vfs_stat(L4_ThreadId_t tid, const char *path, stat_t *buf, int *rval) {
	dprintf(1, "*** vfs_stat: %s, %d, %d, %d, %d, %d\n", path, buf->st_type,
			buf->st_fmode, buf->st_size, buf->st_ctime, buf->st_atime);
	msgClear();
	L4_Reply(tid);
}

