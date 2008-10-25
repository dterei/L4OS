#include <string.h>

#include "constants.h"
#include "console.h"
#include "libsos.h"
#include "nfsfs.h"
#include "process.h"
#include "syscall.h"
#include "vfs.h"

#define verbose 1

#define PS_GET_TID(x) (process_get_tid(process_lookup(x)))

/* The VFS Layer uses callbacks of its own.
 * They are used so that once the FS layer has finished it operations, it can notify
 * the VFS layer (incluiding if the FS layer was successful or not) so that the VFS
 * layer can handle File specific operations such as increasing file pointers,
 * creating and closing files... ect
 *
 * Callbacks will also reply to the thread.
 *
 * The status passed through is check to determine the status of the FS operations.
 */

/* This callback if status is zero or greater will create a filehandler for the address space
 * specified by tid.
 */
static void vfs_open_done(pid_t pid, VNode self, fmode_t mode, int status);

/* This callback will remove the vnode from the global list and reply to the thread */
static void vfs_close_done(pid_t pid, VNode self, fildes_t file, fmode_t mode, int status);

/* Handle the file pointer in the file handler */
static void vfs_read_done(pid_t pid, VNode self, fildes_t file, L4_Word_t pos, char *buf,
		size_t nbyte, int status);

/* Handle the file pointer in the file handler */
static void vfs_write_done(pid_t pid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int status);

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
vfiles_init(fildes_t *fds, VFile *files) {
	for (int i = 0; i < PROCESS_MAX_FILES; i++) {
		files[i].vnode = NULL;
		files[i].fmode = 0;
		files[i].fp = 0;
		files[i].ref = 1;
	}

	for (int i = 0; i < PROCESS_MAX_FDS; i++) {
		fds[i] = VFS_NIL_FILE;
	}
}

/* Test is a file is open (internal SOS function) */
int vfs_isopen(pid_t pid, fildes_t fd) {
	if (get_vfile(pid, fd, 0) != NULL) {
		return 1;
	} else {
		return 0;
	}
}

/* Get the next file descriptor number for an address space
 * Returns two values, the first is the slot in the file descriptor table (fds)
 * and the second is the slot in the open file tabe. Values returned in passed
 * by reference paramaters fds1 and fds2.
 *
 * Return value is 1 for success and 0 for failure.
 */
static
int
findNextFd(Process *p, fildes_t *fds1, fildes_t *fds2) {
	fildes_t *fds = process_get_fds(p);
	VFile *files = process_get_ofiles(p);

	*fds1 = VFS_NIL_FILE;
	*fds2 = VFS_NIL_FILE;

	for (int i = 0; i < PROCESS_MAX_FDS; i++) {
		if (fds[i] == VFS_NIL_FILE) {
			*fds1 = i;
			break;
		}
	}

	for (int i = 0; i < PROCESS_MAX_FILES; i++) {
		if (files[i].vnode == NULL) {
			*fds2 = i;
			break;
		}
	}

	if (*fds1 == VFS_NIL_FILE || *fds2 == VFS_NIL_FILE) {
		// Too many open files.
		return 0;
	} else {
		return 1;
	}
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
	
	if (vnode == NULL) {
		dprintf(0, "!!! vfs: remove_vnode: NULL node passed in\n");
		return;
	}

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

/* Create a new empty vnode */
static
VNode
new_vnode(void) {
	VNode vn = (VNode) malloc(sizeof(struct VNode_t));

	if (vn == NULL) {
		dprintf(0, "!!! vfs_new_vnode: malloc failed!\n");
		return NULL;
	}

	// stats
	vn->path[0] = '\0';
	vn->Max_Readers = FM_UNLIMITED_RW;
	vn->Max_Writers = FM_UNLIMITED_RW;
	vn->readers = 0;
	vn->writers = 0;
	vn->extra = NULL;

	// list pointers
	vn->previous = NULL;
	vn->next = NULL;

	// function pointers
	vn->open = NULL;
	vn->close = NULL;
	vn->read = NULL;
	vn->write = NULL;
	vn->flush = NULL;
	vn->getdirent = NULL;
	vn->stat = NULL;
	vn->remove = NULL;

	return vn;
}

/* Free a vnode */
static
void
free_vnode(VNode vnode) {
	dprintf(1, "free_vnode (%p)\n", vnode);
	if (vnode == NULL) {
		return;
	}
	if (vnode->extra != NULL) {
		free(vnode->extra);
		vnode->extra = NULL;
	}
	free(vnode);
	dprintf(2, "vnode free'd (%p)\n", vnode);
}

/* Handles updating the ref counts */
static
int
increase_refs(VNode vnode, fmode_t mode) {
	// open file for reading
	if (mode & FM_READ) {
		// check if reader slots full
		if (vnode->readers >= vnode->Max_Readers && vnode->Max_Readers != FM_UNLIMITED_RW) {
			return SOS_VFS_READFULL;
		} else {
			vnode->readers++;
		}
	}

	// open file for writing
	if (mode & FM_WRITE) {
		// check if writers slots full
		if (vnode->writers >= vnode->Max_Writers && vnode->Max_Writers != FM_UNLIMITED_RW) {
			if (mode & FM_READ) {
				vnode->readers--;
			}
			return SOS_VFS_WRITEFULL;
		} else {
			vnode->writers++;
		}
	}

	dprintf(2, "Inc VNode: (%s) (r %u/%u) (w %u/%u)\n", vnode->path, vnode->readers,
			vnode->Max_Readers, vnode->writers, vnode->Max_Writers);
	return SOS_VFS_OK;
}

/* Handles updating the ref counts */
static
int
decrease_refs(VNode vnode, fmode_t mode) {
	// close file for reading
	if (mode & FM_READ) {
		vnode->readers--;
	}

	// close file for writing
	if (mode & FM_WRITE) {
		vnode->writers--;
	}

	// check refs are consistent
	if (vnode->readers < 0 || vnode->writers < 0) {
		dprintf(0, "!!! VNode refs corrupt! (%s) (r %u) (w %u)\n", vnode->path,
				vnode->readers, vnode->writers);
		return SOS_VFS_CORVNODE;
	}

	dprintf(2, "Dec VNode: (%s) (r %u/%u) (w %u/%u)\n", vnode->path, vnode->readers,
			vnode->Max_Readers, vnode->writers, vnode->Max_Writers);
	return SOS_VFS_OK;
}

VFile *
get_vfile(pid_t pid, fildes_t file, int ipc) {
	dprintf(1, "get_vfile: %d, %d, %d\n", pid, file, ipc);

	// get file
	Process *p = process_lookup(pid);
	VFile *vf = process_get_ofiles(p);
	fildes_t *fds = process_get_fds(p);
	if (p == NULL || vf == NULL || fds == NULL) {
		dprintf(0, "!!! Process doesn't seem to exist anymore! (p %p) (files %p) (fds %p)\n",
				p, vf, fds);
		return NULL;
	}

	// check values
	if (file < 0 || file >= PROCESS_MAX_FDS || fds[file] < 0 || fds[file] >= PROCESS_MAX_FILES) {
		dprintf(0, "!!! File outside of rang!\n");
		if (ipc) {
			syscall_reply(process_get_tid(p), SOS_VFS_NOFILE);
		}
		return NULL;
	}

	// get vnode
	vf = &vf[fds[file]];
	if (vf->vnode == NULL) {
		dprintf(0, "*** vfs_read: invalid file handler: %d\n", file);
		if (ipc) {
			syscall_reply(process_get_tid(p), SOS_VFS_NOFILE);
		}
		return NULL;
	}

	return vf;
}

/* Open a file, in some cases this just involves increasing a refcount while in others
 * a filesystem must be invoked to handle the call.
 */
void
vfs_open(pid_t pid, const char *path, fmode_t mode,
		unsigned int readers, unsigned int writers) {
	dprintf(1, "*** vfs_open: %d, %p (%s) %d %u %u\n", pid, path, path, mode, readers,
			writers);
	
	VNode vnode = NULL;

	// get file
	Process *p = process_lookup(pid);
	VFile *files = process_get_ofiles(p);
	if (p == NULL || files == NULL) {
		dprintf(0, "!!! Process doesn't seem to exist anymore! (p %p) (files %p)\n", p, files);
		return;
	}

	// check can open more files
	fildes_t dummy;
	if (!findNextFd(p, &dummy, &dummy)) {
		dprintf(2, "*** vfs_open: thread %d can't open more files!\n", pid);
		syscall_reply(process_get_tid(p), SOS_VFS_NOMORE);
		return;
	}

	// check filename is valid
	if (strlen(path) >= MAX_FILE_NAME) {
		dprintf(2, "*** vfs_open: path invalid! thread %d\n", pid);
		syscall_reply(process_get_tid(p), SOS_VFS_PATHINV);
		return;
	}

	// check permissions ok
	if (!(mode & FM_READ) && !(mode & FM_WRITE)) {
		syscall_reply(process_get_tid(p), SOS_VFS_BADMODE);
		return;
	}

	// check for stdin redirection
	if (strncmp(path, STDIN_FN, MAX_FILE_NAME) == 0) {
		fildes_t in = process_get_stdin(p);
		if (in != VFS_NIL_FILE) {
			process_ipcfilt_t oldfilt = process_get_ipcfilt(p);
			process_set_ipcfilt(p, PS_IPC_NONE);
			VFile *vf = get_vfile(pid, in, 1);
			process_set_ipcfilt(p, oldfilt);

			if (vf != NULL) {
				syscall_reply(process_get_tid(p), in);
				return;
			} else {
				// broken so remove and open normal stdin
				process_set_stdin(p, VFS_NIL_FILE);
			}
		}
	}

	// Check open vnodes (special files are stored here)
	vnode = find_vnode(path);

	// Not an open file so open nfs file
	if (vnode == NULL) {
		vnode = new_vnode();
		if (vnode == NULL) {
			dprintf(0, "!!! vfs_open: Malloc Failed! cant create new vnode !!!\n");
			vfs_open_done(pid, vnode, mode, SOS_VFS_NOMEM);
			return;
		}

		// setup locking and make sure valid paramaters
		vnode->Max_Readers = readers;
		vnode->Max_Writers = writers;
		if (increase_refs(vnode, mode) != SOS_VFS_OK) {
			free_vnode(vnode);
			syscall_reply(process_get_tid(p), SOS_VFS_ERROR);
		}
		vnode->readers = 0;
		vnode->writers = 0;

		dprintf(2, "*** vfs_open: try to open file with nfs: %s\n", path);
		nfsfs_open(pid, vnode, path, mode, vfs_open_done);
		add_vnode(vnode);
	}

	// Open file, so handle in just vfs layer
	else {
		// can only lock non - open files!
		if (readers == FM_UNLIMITED_RW && writers == FM_UNLIMITED_RW) {
			vfs_open_done(pid, vnode, mode, SOS_VFS_OK);
		} else {
			syscall_reply(process_get_tid(p), SOS_VFS_OPEN);
		}
	}
}

static
void
vfs_open_err(VNode self) {
	if (self != NULL && self->readers <= 0 && self->writers <= 0) {
		remove_vnode(self);
		free_vnode(self);
	}
}

/* This callback if status is zero or greater will create a filehandler for the address space
 * specified by tid.
 */
static
void
vfs_open_done(pid_t pid, VNode self, fmode_t mode, int status) {
	dprintf(1, "*** vfs_open_done: %d %p %d %d\n", pid, self, mode, status);

	// get file
	Process *p = process_lookup(pid);
	VFile *vf = process_get_ofiles(p);
	fildes_t *fds = process_get_fds(p);
	if (p == NULL || vf == NULL || fds == NULL) {
		dprintf(0, "!!! Process doesn't seem to exist anymore! (p %p) (files %p) (fds %p)\n",
				p, vf, fds);
		vfs_open_err(self);
		return;
	}

	// open failed
	if (status != SOS_VFS_OK || self == NULL) {
		dprintf(0, "*** vfs_open_done: can't open file: error code %d\n", status);
		vfs_open_err(self);
		syscall_reply(process_get_tid(p), status);
		return;
	}

	// increase ref counts (should be zero since its a new vnode)
	int rval = increase_refs(self, mode);
	if (rval != SOS_VFS_OK) {
		dprintf(0, "!!! vfs_open_done: file opened too many times (r %u/%u) (w %u/%u)\n",
				self->readers, self->Max_Readers, self->writers, self->Max_Writers);
		vfs_open_err(self);
		syscall_reply(process_get_tid(p), rval);
		return;
	}

	// get new fd
	fildes_t fd1, fd2;
	if (!findNextFd(p, &fd1, &fd2)) {
		dprintf(0, "!!! vfs_open_done: thread %d can't open more files!\n", pid);
		decrease_refs(self, mode);
		vfs_open_err(self);
		syscall_reply(process_get_tid(p), SOS_VFS_NOMORE);
		return;
	}

	// store file in per process table
	vf[fd2].vnode = self;
	vf[fd2].fmode = mode;
	vf[fd2].fp = 0;
	vf[fd2].ref = 1;

	// store pointer to file in redirect table
	fds[fd1] = fd2;

	syscall_reply(process_get_tid(p), fd1);
}

/* Close a file */
void
vfs_close(pid_t pid, fildes_t file) {
	dprintf(1, "*** vfs_close: %d\n", file);
	
	// get file
	VFile *vf = get_vfile(pid, file, 0);
	if (vf == NULL) return;

	// close the fds table entry
	Process *p = process_lookup(pid);
	process_get_fds(p)[file] = VFS_NIL_FILE;

	int mode = vf->fmode;
	VNode vnode = vf->vnode;

	// close file table entry
	vf->ref--;
	if (vf->ref == 0) {
		vf->vnode = NULL;
		vf->fmode = 0;
		vf->fp = 0;

		decrease_refs(vnode, mode);
	}

	// close vnode if no longer referenced
	if (vnode->readers <= 0 && vnode->writers <= 0) {
		vnode->close(pid, vnode, file, mode, vfs_close_done);
	} else {
		syscall_reply(PS_GET_TID(pid), SOS_VFS_OK);
	}
}

/* This callback will remove the vnode from the global list and reply to the thread */
static
void
vfs_close_done(pid_t pid, VNode self, fildes_t file, fmode_t mode, int status) {
	dprintf(1, "*** vfs_close_done: %d %p %d\n", file, self, status);

	if (status == SOS_VFS_OK) {
		// close global vnode entry
		remove_vnode(self);
		free_vnode(self);
	}
	syscall_reply(PS_GET_TID(pid), status);
}

/* Read from a file */
void
vfs_read(pid_t pid, fildes_t file, char *buf, size_t nbyte) {
	dprintf(1, "*** vfs_read: %d %d %p %d\n", pid, file, buf, nbyte);

	// get file
	VFile *vf = get_vfile(pid, file, 1);
	if (vf == NULL) return;

	// check permissions
	if (!(vf->fmode & FM_READ)) {
		dprintf(1, "*** vfs_read: invalid read permissions for file: %d, %d\n",
				file, vf->fmode);
		syscall_reply(PS_GET_TID(pid), SOS_VFS_PERM);
		return;
	}
	
	// restrict max buffer size
	if (nbyte > IO_MAX_BUFFER) {
		dprintf(2, "vfs_read: tried to read too much data at once: %d\n", nbyte);
		nbyte = IO_MAX_BUFFER;
	}

	vf->vnode->read(pid, vf->vnode, file, vf->fp, buf, nbyte, vfs_read_done);
}

/* Handle the file pointer in the file handler, status is set already by the fs layer
 * to the value that should be returned to the user, while nbyte is used to tell
 * the vfs layer how much to change the file pos pointer by. These may differ
 * for special file systems such as console where you never want to change the
 * file pos pointer.
 */
static
void
vfs_read_done(pid_t pid, VNode self, fildes_t file, L4_Word_t pos, char *buf,
		size_t nbyte, int status) {
	dprintf(1, "*** vfs_read_done: %d %d %p %d %d\n", pid, file, buf, nbyte, status);

	// get file
	VFile *vf = get_vfile(pid, file, 1);
	if (vf == NULL) return;
	
	// check no error
	if (status < 0) {
		syscall_reply(PS_GET_TID(pid), status);
		return;
	}

	// update file
	vf->fp += nbyte;
	syscall_reply(PS_GET_TID(pid), status);
}

/* Write to a file */
void
vfs_write(pid_t pid, fildes_t file, const char *buf, size_t nbyte) {
	dprintf(1, "*** vfs_write: %d, %d %p %d\n", pid, file, buf, nbyte);

	// get file
	VFile *vf = get_vfile(pid, file, 1);
	if (vf == NULL) return;

	// check permissions
	if (!(vf->fmode & FM_WRITE)) {
		dprintf(1, "*** vfs_write: invalid write permissions for file: %d, %s, %d\n",
				file, vf->vnode->path, vf->fmode);
		syscall_reply(PS_GET_TID(pid), SOS_VFS_PERM);
		return;
	}

	// restrict max buffer size
	if (nbyte > IO_MAX_BUFFER) {
		dprintf(2, "vfs_write: tried to write too much data at once: %d\n", nbyte);
		nbyte = IO_MAX_BUFFER;
	}

	vf->vnode->write(pid, vf->vnode, file, vf->fp, buf, nbyte, vfs_write_done);
}

/* Handle the file pointer in the file handler, status is set already by the fs layer
 * to the value that should be returned to the user, while nbyte is used to tell
 * the vfs layer how much to change the file pos pointer by. These may differ
 * for special file systems such as console where you never want to change the
 * file pos pointer.
 */
static
void
vfs_write_done(pid_t pid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int status) {
	dprintf(1, "*** vfs_write_done: %d %d %p %d %d\n", pid, file,
			buf, nbyte, status);

	if (status < 0) {
		syscall_reply(PS_GET_TID(pid), status);
		return;
	}
	
	// get file
	VFile *vf = get_vfile(pid, file, 1);
	if (vf == NULL) return;
	
	vf->fp += nbyte;
	syscall_reply_v(PS_GET_TID(pid), 2, status, SOS_WRITE);
}

/* Flush a stream */
void
vfs_flush(pid_t pid, fildes_t file) {
	dprintf(1, "*** vfs_flush: %d, %d\n", pid, file);

	// get file
	VFile *vf = get_vfile(pid, file, 0);
	if (vf == NULL) return;

	dprintf(1, "flush vnode\n");
	// flush
	vf->vnode->flush(pid, vf->vnode, file);
}

/* Seek to a position in a file */
void
vfs_lseek(pid_t pid, fildes_t file, fpos_t pos, int whence) {
	dprintf(1, "*** vfs_seek: %d, %d %p %d\n", pid, file, pos, whence);

	// get file
	VFile *vf = get_vfile(pid, file, 1);
	if (vf == NULL) return;

	dprintf(3, "vfs_seek: old fp %d\n", vf->fp);

	if (whence == SEEK_SET) {
		vf->fp = (L4_Word_t) pos;
	} else if (whence == SEEK_CUR) {
		vf->fp += pos;
	} else if (whence == SEEK_END) {
		vf->fp = vf->vnode->vstat.st_size - pos;
	} else {
		dprintf(1, "!!! vfs_lseek: invalid value for whence\n");
	}

	dprintf(3, "vfs_seek: new fp %d\n", vf->fp);

	syscall_reply(PS_GET_TID(pid), SOS_VFS_OK);
}

/* Get a directory listing */
void
vfs_getdirent(pid_t pid, int pos, char *name, size_t nbyte) {
	dprintf(1, "*** vfs_getdirent: %d, %p, %d\n", pos, name, nbyte);

	// check
	if (pos < 0 || nbyte <= 0 || name == NULL) {
		syscall_reply(PS_GET_TID(pid), SOS_VFS_ERROR);
		return;
	}
	
	// print out any special files
	int pos2 = 0;
	for (VNode vnode = GlobalVNodes; vnode != NULL; vnode = vnode->next) {
		if (vnode->vstat.st_type == ST_SPECIAL) {

			if (pos == pos2) {
				int nlen = strnlen(vnode->path, MAX_FILE_NAME);

				if (nlen < nbyte) {
					memcpy(name, vnode->path, nlen);
					name[nlen] = '\0';
					syscall_reply(PS_GET_TID(pid), nlen);
				} else {
					dprintf(0, "!!! vfs_getdirent: Filename too big for given buffer! (%d) (%d)\n",
							nlen, nbyte);
					syscall_reply(PS_GET_TID(pid), SOS_VFS_NOMEM);
				}
				return;
			}

			pos2++;
		}
	}

	// Only support nfs fs for moment
	nfsfs_getdirent(pid, NULL, pos - pos2, name, nbyte);
}

/* Stat a file */
void
vfs_stat(pid_t pid, const char *path, stat_t *buf) {
	dprintf(1, "*** vfs_stat: %s\n", path);
	
	// Check open vnodes
	VNode vnode = find_vnode(path);
	if (vnode != NULL) {
		dprintf(1, "*** vfs_stat: found already open vnode: %s ***\n", vnode->path);
		vnode->stat(pid, vnode, path, buf);
	}
	// Not open so assume nfs
	else {
		nfsfs_stat(pid, NULL, path, buf);
	}
}

/* Remove a file */
void
vfs_remove(pid_t pid, const char *path) {
	dprintf(1, "*** vfs_remove: %d %s ***\n", pid, path);
	
	// Check open vnodes
	VNode vnode = find_vnode(path);
	if (vnode != NULL) {
		dprintf(1, "*** vfs_remove: found already open vnode: %s ***\n", vnode->path);

		// check permissions
		if (!(vnode->vstat.st_fmode & FM_WRITE)) {
			dprintf(1, "vnfs_remove: no write permission for file, can't remove (%d)\n",
					vnode->vstat.st_fmode);
			syscall_reply(PS_GET_TID(pid), SOS_VFS_PERM);
			return;
		}

		// this will fail for nfs and console fs, can only remove non open files.
		vnode->remove(pid, vnode, path);
	}
	// not open so assume nfs
	else {
		nfsfs_remove(pid, NULL, path);
	}
}

/* Duplicate the given file descriptor to the one specified */
void
vfs_dup(pid_t pid, fildes_t forig, fildes_t fdup) {
	dprintf(1, "*** vfs_dup: %d, %d, %d\n", pid, forig, fdup);

	// get file
	VFile *vf = get_vfile(pid, forig, 1);
	if (vf == NULL) {
		return;
	}

	Process *p = process_lookup(pid);

	if ((fdup < 0 || fdup >= PROCESS_MAX_FDS) && fdup != VFS_NIL_FILE) {
		syscall_reply(process_get_tid(p), SOS_VFS_NOFILE);
		return;
	}

	fildes_t *fds = process_get_fds(p);

	if (fdup == VFS_NIL_FILE) {
		for (int i = 0; i < PROCESS_MAX_FDS; i++) {
			if (fds[i] == VFS_NIL_FILE) {
				fds[i] = fds[forig];
				fdup = i;
				break;
			}
		}
	} else {
		VFile *dvf = get_vfile(pid, fdup, 0);
		if (dvf != NULL) {
			// change filter to none so we can close the file first
			process_ipcfilt_t oldfilt = process_set_ipcfilt(p, PS_IPC_NONE);
			vfs_close(pid, fdup);
			process_set_ipcfilt(p, oldfilt);
		}
		fds[fdup] = fds[forig];
	}

	if (fdup == VFS_NIL_FILE) {
		syscall_reply(process_get_tid(p), SOS_VFS_NOMORE);
	} else {
		vf->ref++;	
		syscall_reply(process_get_tid(p), fdup);
	}
}

