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
#include "libsos.h"
#include "vfs.h"
#include "console.h"
#include "nfsfs.h"
#include "pager.h"
#include "syscall.h"

#define verbose 1

// Global open vnodes list
VNode GlobalVNodes;

// Per process open file table
VFile_t openfiles[MAX_ADDRSPACES][PROCESS_MAX_FILES];

void
vfs_init(void) {
	GlobalVNodes = NULL;

	// All vnodes are unallocated
	for (int i = 0; i < MAX_ADDRSPACES; i++) {
		for (int j = 0; j < PROCESS_MAX_FILES; j++) {
			openfiles[i][j].vnode = NULL;
			openfiles[i][j].fmode = 0;
			openfiles[i][j].fp = 0;
		}
	}

	// Init file systems
	dprintf(1, "*** vfs_init\n");
	GlobalVNodes = console_init(GlobalVNodes);
	nfsfs_init();
}

void
vfs_open(L4_ThreadId_t tid, const char *path, fmode_t mode, int *rval) {
	dprintf(1, "*** vfs_open: %d, %p (%s) %d\n", L4_ThreadNo(tid),
			path, path, mode);
	VNode vnode = NULL;

	// check can open more files
	if (findNextFd(L4_ThreadNo(tid)) < 0) {
		dprintf(0, "*** vfs_open: thread %d can't open more files!\n", L4_ThreadNo(tid));
		*rval = (-1);
		syscall_reply(tid);
		return;
	}

	// check filename is valid
	if (strlen(path) >= N_NAME) {
		dprintf(0, "*** vfs_open: path invalid! thread %d\n", L4_ThreadNo(tid));
		*rval = (-1);
		syscall_reply(tid);
		return;
	}

	// Check open vnodes (special files are stored here)
	for (vnode = GlobalVNodes; vnode != NULL; vnode = vnode->next) {
		dprintf(1, "*** vfs_open: vnode list item: %s, %p, %p, %p***\n", vnode->path, vnode, vnode->next, vnode->previous);
		if (strcmp(vnode->path, path) == 0) {
			dprintf(1, "*** vfs_open: found already open vnode: %s ***\n", vnode->path);
			break;
		}
	}

	if (vnode == NULL) {
		// Not an open file so open nfs file
		dprintf(1, "*** vfs_open: try to open file with nfs: %s\n", path);
		nfsfs_open(tid, vnode, path, mode, rval, vfs_open_done);
	} else {
		// Have vnode now so open
		vnode->open(tid, vnode, path, mode, rval, vfs_open_done);
	}
}

void
vfs_open_done(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode, int *rval) {
	dprintf(1, "*** vfs_open_done: %d %p (%s) %d %d\n", L4_ThreadNo(tid), path, path, mode, *rval);
	//TODO: Move opening of already open vnode to the vfs layer, requires moving the
	//max readers and writers variables into the vnode struct rather then console.

	// open failed
	if (*rval < 0 || self == NULL) {
		dprintf(1, "*** vfs_open_done: can't open file: error code %d\n", *rval);
		return;
	}

	fildes_t fd = findNextFd(L4_ThreadNo(tid));
	*rval = fd;

	// store file in per process table
	openfiles[L4_ThreadNo(tid)][fd].vnode = self;
	openfiles[L4_ThreadNo(tid)][fd].fmode = mode;
	openfiles[L4_ThreadNo(tid)][fd].fp = 0;

	// update global vnode list if not already on it
	if (self->next == NULL && self->previous == NULL && self != GlobalVNodes) {
		dprintf(1, "*** vfs_open_done: add to vnode list (%s), %p, %p, %p, %d\n", path,
				self, self->next, self->previous, fd);

		self->next = NULL;
		self->previous = NULL;

		// add to list if list not empty
		if (GlobalVNodes != NULL) {
			self->next = GlobalVNodes;
			GlobalVNodes->previous = self;
		}

		GlobalVNodes = self;
	} else {
		dprintf(1, "*** vfs_open_done: already on vnode list (%s), %p, %p, %p, %d\n", path,
				self, self->next, self->previous, fd);
	}

	// update vnode refcount
	self->refcount++;
}

void
vfs_close(L4_ThreadId_t tid, fildes_t file, int *rval) {
	dprintf(1, "*** vfs_close: %d\n", file);

	// get file
	VFile_t *vf = &openfiles[L4_ThreadNo(tid)][file];

	// get vnode
	VNode vnode =vf->vnode;
	if (vnode == NULL) {
		dprintf(1, "*** vfs_close: invalid file handler: %d\n", file);
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
	VFile_t *vf = &openfiles[L4_ThreadNo(tid)][file];
	VNode vnode = vf->vnode;

	// clean up book keeping
	if (*rval == 0) {
		// close file table entry
		vf->vnode = NULL;
		vf->fmode = 0;
		vf->fp = 0;

		// close global vnode entry if returned vnode is null
		if (vnode == NULL) {
			VNode vp = vnode->previous;
			VNode vn = vnode->next;

			if (vp == NULL && vn == NULL) {
				GlobalVNodes = NULL;
			} else if (vn == NULL) {
				vp->next = NULL;
			} else if (vp == NULL) {
				GlobalVNodes = vn;
				vn->previous = NULL;
			} else {
				vp->next = vn;
				vn->previous = vp;
			}
		} else {
			vnode->refcount--;
		}
	}
}

void
vfs_read(L4_ThreadId_t tid, fildes_t file, char *buf, size_t nbyte, int *rval) {
	dprintf(1, "*** vfs_read: %d %d %d %p %d\n", L4_ThreadNo(tid),
			L4_ThreadNo(tid), file, buf, nbyte);

	// get file
	VFile_t *vf = &openfiles[L4_ThreadNo(tid)][file];

	// get vnode
	VNode vnode = vf->vnode;
	if (vnode == NULL) {
		dprintf(1, "*** vfs_read: invalid file handler: %d\n", file);
		*rval = (-1);
		syscall_reply(tid);
		return;
	}

	// check permissions
	if (!(vf->fmode & FM_READ)) {
		dprintf(1, "*** vfs_read: invalid read permissions for file: %d, %d\n",
				file, vf->fmode);
		*rval = (-1);
		syscall_reply(tid);
		return;
	}

	vnode->read(tid, vnode, file, vf->fp, buf, nbyte, rval, vfs_read_done);
}

void
vfs_read_done(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos, char *buf,
		size_t nbyte, int *rval) {
	dprintf(1, "*** vfs_read_done: %d %d %d %p %d %d\n", L4_ThreadNo(tid),
			L4_ThreadNo(tid), file, buf, nbyte, *rval);

	if (*rval < 0) {
		return;
	}
	
	// XXX This seems to work but check since seems strange, I guess perhaps callback use some magic
	// to appear from the space which called them
	openfiles[L4_ThreadNo(tid)][file].fp += nbyte;
}

void
vfs_write(L4_ThreadId_t tid, fildes_t file, const char *buf, size_t nbyte, int *rval) {
	dprintf(1, "*** vfs_write: %d, %d %p %d\n", L4_ThreadNo(tid), file, buf, nbyte);

	// get file
	VFile_t *vf = &openfiles[L4_ThreadNo(tid)][file];

	// get vnode
	VNode vnode = vf->vnode;
	if (vnode == NULL) {
		dprintf(1, "*** vfs_write: invalid file handler: %d\n", file);
		*rval = (-1);
		syscall_reply(tid);
		return;
	}

	// check permissions
	if (!(vf->fmode & FM_WRITE)) {
		dprintf(1, "*** vfs_write: invalid write permissions for file: %d, %s, %d\n",
				file, vnode->path, vf->fmode);
		//*rval = (-1);
		//syscall_reply(tid);
		//return;
	}

	vnode->write(tid, vnode, file, vf->fp, buf, nbyte, rval, vfs_write_done);
}

void
vfs_write_done(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int *rval) {
	dprintf(1, "*** vfs_write_done: %d %d %d %p %d %d\n", L4_ThreadNo(tid),
			L4_ThreadNo(tid), file, buf, nbyte, *rval);

	if (*rval < 0) {
		return;
	}
	
	// XXX This seems to work but check since seems strange, I guess perhaps callback use some magic
	// to appear from the space which called them
	openfiles[L4_ThreadNo(tid)][file].fp += nbyte;
}

void
vfs_getdirent(L4_ThreadId_t tid, int pos, char *name, size_t nbyte, int *rval) {
	dprintf(1, "*** vfs_getdirent: %d, %p, %d\n", pos, name, nbyte);

	nfsfs_getdirent(tid, NULL, pos, name, nbyte, rval);
}

void
vfs_stat(L4_ThreadId_t tid, const char *path, stat_t *buf, int *rval) {
	dprintf(1, "*** vfs_stat: %s, %d, %d, %d, %d, %d\n", path, buf->st_type,
			buf->st_fmode, buf->st_size, buf->st_ctime, buf->st_atime);
	
	// Check open vnodes (special files are stored here)
	for (VNode vnode = GlobalVNodes; vnode != NULL; vnode = vnode->next) {
		dprintf(1, "*** vfs_stat: vnode list item: %s, %p, %p, %p***\n", vnode->path, vnode, vnode->next, vnode->previous);
		if (strcmp(vnode->path, path) == 0) {
			dprintf(1, "*** vfs_stat: found already open vnode: %s ***\n", vnode->path);
			vnode->stat(tid, vnode, path, buf, rval);
			return;
		}
	}

	// not open so assume nfs
	nfsfs_stat(tid, NULL, path, buf, rval);
}

fildes_t
findNextFd(int spaceId) {
	for (int i = 0; i < MAX_ADDRSPACES; i++) {
		if (openfiles[spaceId][i].vnode == NULL) return i;
	}

	// Too many open files.
	return (-1);
}

