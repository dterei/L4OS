#include <string.h>

#include "constants.h"
#include "console.h"
#include "libsos.h"
#include "nfsfs.h"
#include "process.h"
#include "syscall.h"
#include "vfs.h"

#define verbose 1

/* The VFS Layer uses callbacks of its own.
 * They are used so that once the FS layer has finished it operations, it can notify
 * the VFS layer (incluiding if the FS layer was successful or not) so that the VFS
 * layer can handle File specific operations such as increasing file pointers,
 * creating and closing files... ect
 *
 * The rval passed through is check to determine the status of the FS operations.
 */

/* This callback if rval is zero or greater will create a filehandler for the address space
 * specified by tid.
 */
static void vfs_open_done(L4_ThreadId_t tid, VNode self, fmode_t mode, int status);

/* This callback will decrease the refcount for the file handler, if the vnode returned is
 * null, then the filehandler is also closed.
 */
static void vfs_close_done(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode, int status);

/* Handle the file pointer in the file handler */
static void vfs_read_done(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos, char *buf,
		size_t nbyte, int *rval);

/* Handle the file pointer in the file handler */
static void vfs_write_done(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int *rval);

// Global open vnodes list
static VNode GlobalVNodes;

/* Initialise the VFS Layer */
void
vfs_init(void) {
	// Init file systems
	dprintf(1, "*** vfs_init\n");
	GlobalVNodes = console_init(GlobalVNodes);
	nfsfs_init();
}

/* Initialise an array of VFiles */
void
vfiles_init(VFile *files) {
	for (int i = 0; i < PROCESS_MAX_FILES; i++) {
		files[i].vnode = NULL;
		files[i].fmode = 0;
		files[i].fp = 0;
	}
}

/* Get the next file descriptor number for an address space */
static
fildes_t
findNextFd(Process *p) {
	VFile *files = process_get_files(p);

	for (int i = 0; i < PROCESS_MAX_FILES; i++) {
		if (files[i].vnode == NULL) return i;
	}

	// Too many open files.
	return (-1);
}

/* Add vnode to global list */
static
void
add_vnode(VNode vnode) {
	vnode->next = NULL;
	vnode->previous = NULL;

	// add to list if list not empty
	if (GlobalVNodes != NULL) {
		vnode->next = GlobalVNodes;
		GlobalVNodes->previous = vnode;
	}

	GlobalVNodes = vnode;
}

/* Remove vnode from global list */
static
void
remove_vnode(VNode vnode) {
	dprintf(2, "*** remove_vnode: %p\n", vnode);

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

	vnode->next = NULL;
	vnode->previous = NULL;
	
	dprintf(2, "vnode removed\n", vnode);
}

/* Find vnode on global list */
static
VNode
find_vnode(const char *path) {
	for (VNode vnode = GlobalVNodes; vnode != NULL; vnode = vnode->next) {
		dprintf(1, "*** vfs_open: vnode list item: %s, %p, %p, %p***\n",
				vnode->path, vnode, vnode->next, vnode->previous);
		if (strcmp(vnode->path, path) == 0) {
			dprintf(1, "*** vfs_open: found already open vnode: %s ***\n", vnode->path);
			return vnode;
		}
	}

	return NULL;
}

/* Open a file, in some cases this just involves increasing a refcount while in others
 * a filesystem must be invoked to handle the call.
 */
void
vfs_open(L4_ThreadId_t tid, const char *path, fmode_t mode) {
	dprintf(1, "*** vfs_open: %d, %p (%s) %d\n", L4_ThreadNo(tid),
			path, path, mode);
	
	// TODO: Move opening of already open vnode to the vfs layer, requires moving the
	// max readers and writers variables into the vnode struct rather then console.

	VNode vnode = NULL;

	// check can open more files
	if (findNextFd(process_lookup(L4_ThreadNo(tid))) < 0) {
		dprintf(1, "*** vfs_open: thread %d can't open more files!\n", L4_ThreadNo(tid));
		syscall_reply(tid, SOS_VFS_NOMORE);
		return;
	}

	// check filename is valid
	if (strlen(path) >= N_NAME) {
		dprintf(1, "*** vfs_open: path invalid! thread %d\n", L4_ThreadNo(tid));
		syscall_reply(tid, SOS_VFS_PATHINV);
		return;
	}

	// Check open vnodes (special files are stored here)
	vnode = find_vnode(path);

	if (vnode == NULL) {
		// Not an open file so open nfs file
		dprintf(1, "*** vfs_open: try to open file with nfs: %s\n", path);
		nfsfs_open(tid, vnode, path, mode, vfs_open_done);
	} else {
		// Have vnode now so open
		vnode->open(tid, vnode, path, mode, vfs_open_done);
	}
}

/* This callback if rval is zero or greater will create a filehandler for the address space
 * specified by tid.
 */
static
void
vfs_open_done(L4_ThreadId_t tid, VNode self, fmode_t mode, int status) {
	dprintf(1, "*** vfs_open_done: %d %p %d %d\n", L4_ThreadNo(tid), self, mode, status);

	// open failed
	if (status != SOS_VFS_OK || self == NULL) {
		dprintf(1, "*** vfs_open_done: can't open file: error code %d\n", status);
		syscall_reply(tid, status);
		return;
	}

	Process *p = process_lookup(L4_ThreadNo(tid));
	fildes_t fd = findNextFd(p);

	// store file in per process table
	VFile *files = process_get_files(p);
	files[fd].vnode = self;
	files[fd].fmode = mode;
	files[fd].fp = 0;

	// update global vnode list if not already on it
	if (self->next == NULL && self->previous == NULL && self != GlobalVNodes) {
		dprintf(1, "*** vfs_open_done: add to vnode list (%s), %p, %p, %p, %d\n", self->path,
				self, self->next, self->previous, fd);

		add_vnode(self);
	} else {
		dprintf(1, "*** vfs_open_done: already on vnode list (%s), %p, %p, %p, %d\n", self->path,
				self, self->next, self->previous, fd);
	}

	// update vnode refcount
	self->refcount++;
	syscall_reply(tid, fd);
}

/* Close a file */
void
vfs_close(L4_ThreadId_t tid, fildes_t file) {
	dprintf(1, "*** vfs_close: %d\n", file);
	
	//TODO: Move closing a vnode to the vfs layer (for refcount stuff anyway).

	// get file
	Process *p = process_lookup(L4_ThreadNo(tid));
	VFile *vf = &process_get_files(p)[file];

	// get vnode
	VNode vnode =vf->vnode;
	if (vnode == NULL) {
		dprintf(1, "*** vfs_close: invalid file handler: %d\n", file);
		syscall_reply(tid, SOS_VFS_NOFILE);
		return;
	}

	// vnode close function is responsible for freeing the node if appropriate
	// so don't access after a close without checking its not null
	// try closing vnode;
	vnode->close(tid, vnode, file, vf->fmode, vfs_close_done);
}

/* This callback will decrease the refcount for the file handler, if the vnode returned is
 * null, then the filehandler is also closed.
 */
static
void
vfs_close_done(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode, int status) {
	dprintf(1, "*** vfs_close_done: %d %p %d\n", file, self, status);

	// close failed
	if (status != SOS_VFS_OK) {
		dprintf(1, "*** vfs_close_done: can't close file: error code %d\n", status);
		syscall_reply(tid, status);
		return;
	}

	// get file & vnode
	Process *p = process_lookup(L4_ThreadNo(tid));
	VFile *vf = &process_get_files(p)[file];
	VNode vnode = vf->vnode;

	// close file table entry
	vf->vnode = NULL;
	vf->fmode = 0;
	vf->fp = 0;

	// close global vnode entry if returned vnode is null
	if (self == NULL && vnode != NULL) {
		remove_vnode(vnode);
		free(vnode);
	} else if (vnode == NULL) {
		dprintf(0, "!!! vfs_close_done: Error closing vnode, already NULL (%d %d, %p, %p, %d)!!!\n",
				L4_ThreadNo(tid), file, self, vnode, status);
	} else {
		vnode->refcount--;
	}

	syscall_reply(tid, status);
}

/* Read from a file */
void
vfs_read(L4_ThreadId_t tid, fildes_t file, char *buf, size_t nbyte, int *rval) {
	dprintf(1, "*** vfs_read: %d %d %p %d\n", L4_ThreadNo(tid), file, buf, nbyte);

	// get file
	Process *p = process_lookup(L4_ThreadNo(tid));
	VFile *vf = &process_get_files(p)[file];

	// get vnode
	VNode vnode = vf->vnode;
	if (vnode == NULL) {
		dprintf(1, "*** vfs_read: invalid file handler: %d\n", file);
		*rval = SOS_VFS_NOFILE;
		syscall_reply(tid, *rval);
		return;
	}

	// check permissions
	if (!(vf->fmode & FM_READ)) {
		dprintf(1, "*** vfs_read: invalid read permissions for file: %d, %d\n",
				file, vf->fmode);
		*rval = SOS_VFS_PERM;
		syscall_reply(tid, *rval);
		return;
	}

	vnode->read(tid, vnode, file, vf->fp, buf, nbyte, rval, vfs_read_done);
}

/* Handle the file pointer in the file handler, rval is set already by the fs layer
 * to the value that should be returned to the user, while nbyte is used to tell
 * the vfs layer how much to change the file pos pointer by. These may differ
 * for special file systems such as console where you never want to chang the
 * file pos pointer.
 */
static
void
vfs_read_done(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos, char *buf,
		size_t nbyte, int *rval) {
	dprintf(1, "*** vfs_read_done: %d %d %d %p %d %d\n", L4_ThreadNo(tid),
			L4_ThreadNo(tid), file, buf, nbyte, *rval);

	if (*rval < 0) {
		return;
	}

	Process *p = process_lookup(L4_ThreadNo(tid));
	process_get_files(p)[file].fp += nbyte;
}

/* Write to a file */
void
vfs_write(L4_ThreadId_t tid, fildes_t file, const char *buf, size_t nbyte, int *rval) {
	dprintf(1, "*** vfs_write: %d, %d %p %d\n", L4_ThreadNo(tid), file, buf, nbyte);

	// get file
	Process *p = process_lookup(L4_ThreadNo(tid));
	VFile *vf = &process_get_files(p)[file];

	// get vnode
	VNode vnode = vf->vnode;
	if (vnode == NULL) {
		dprintf(1, "*** vfs_write: invalid file handler: %d\n", file);
		*rval = SOS_VFS_NOFILE;
		syscall_reply(tid, *rval);
		return;
	}

	// check permissions
	if (!(vf->fmode & FM_WRITE)) {
		dprintf(1, "*** vfs_write: invalid write permissions for file: %d, %s, %d\n",
				file, vnode->path, vf->fmode);
		*rval = SOS_VFS_PERM;
		syscall_reply(tid, *rval);
		return;
	}

	vnode->write(tid, vnode, file, vf->fp, buf, nbyte, rval, vfs_write_done);
}

/* Handle the file pointer in the file handler, rval is set already by the fs layer
 * to the value that should be returned to the user, while nbyte is used to tell
 * the vfs layer how much to change the file pos pointer by. These may differ
 * for special file systems such as console where you never want to change the
 * file pos pointer.
 */
static
void
vfs_write_done(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int *rval) {
	dprintf(1, "*** vfs_write_done: %d %d %d %p %d %d\n", L4_ThreadNo(tid),
			L4_ThreadNo(tid), file, buf, nbyte, *rval);

	if (*rval < 0) {
		return;
	}
	
	Process *p = process_lookup(L4_ThreadNo(tid));
	process_get_files(p)[file].fp += nbyte;
}

/* Seek to a position in a file */
void
vfs_lseek(L4_ThreadId_t tid, fildes_t file, fpos_t pos, int whence, int *rval) {
	dprintf(0, "*** vfs_seek: %d, %d %p %d\n", L4_ThreadNo(tid), file, pos, whence);

	// get file
	Process *p = process_lookup(L4_ThreadNo(tid));
	VFile *vf = NULL;
	if (p != NULL) {
		 vf = &process_get_files(p)[file];
	}

	// get vnode to make sure file exists
	VNode vnode = vf->vnode;

	// make sure ok
	if (p == NULL || vf == NULL || vnode == NULL) {
		dprintf(0, "*** vfs_seek: invalid file handler: %d\n", file);
		*rval = SOS_VFS_NOFILE;
		syscall_reply(tid, *rval);
		return;
	}

	dprintf(0, "vfs_seek: old fp %d\n", vf->fp);

	if (whence == SEEK_SET) {
		vf->fp = (L4_Word_t) pos;
	} else if (whence == SEEK_CUR) {
		vf->fp += pos;
	} else if (whence == SEEK_END) {
		vf->fp = vnode->vstat.st_size - pos;
	} else {
		dprintf(0, "!!! vfs_lseek: invalid value for whence\n");
	}

	dprintf(0, "vfs_seek: new fp %d\n", vf->fp);

	*rval = SOS_VFS_OK;
	syscall_reply(tid, *rval);
}

/* Get a directory listing */
void
vfs_getdirent(L4_ThreadId_t tid, int pos, char *name, size_t nbyte, int *rval) {
	dprintf(1, "*** vfs_getdirent: %d, %p, %d\n", pos, name, nbyte);

	nfsfs_getdirent(tid, NULL, pos, name, nbyte, rval);
}

/* Stat a file */
void
vfs_stat(L4_ThreadId_t tid, const char *path, stat_t *buf, int *rval) {
	dprintf(1, "*** vfs_stat: %s\n", path);
	
	// Check open vnodes
	VNode vnode = find_vnode(path);
	if (vnode != NULL) {
		dprintf(1, "*** vfs_stat: found already open vnode: %s ***\n", vnode->path);
		vnode->stat(tid, vnode, path, buf, rval);
	}
	// not open so assume nfs
	else {
		nfsfs_stat(tid, NULL, path, buf, rval);
	}
}

/* Remove a file */
void vfs_remove(L4_ThreadId_t tid, const char *path, int *rval) {
	dprintf(1, "*** vfs_remove: %d %s ***\n", L4_ThreadNo(tid), path);
	
	// Check open vnodes
	VNode vnode = find_vnode(path);
	if (vnode != NULL) {
		dprintf(1, "*** vfs_remove: found already open vnode: %s ***\n", vnode->path);

		// check permissions
		if (!(vnode->vstat.st_fmode & FM_WRITE)) {
			dprintf(1, "vnfs_remove: no write permission for file, can't remove (%d)\n",
					vnode->vstat.st_fmode);
			*rval = SOS_VFS_PERM;
			syscall_reply(tid, *rval);
			return;
		}

		// this will fail for nfs and console fs, can only remove non open files.
		vnode->remove(tid, vnode, path, rval);
	}
	// not open so assume nfs
	else {
		nfsfs_remove(tid, NULL, path, rval);
	}
}

