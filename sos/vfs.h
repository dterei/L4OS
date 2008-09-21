#ifndef _VFS_H
#define _VFS_H

#include <sos/sos.h>

/* Simple VFS-style vnode */
typedef struct VNode_t *VNode;

/* All allocated vnodes stored in double link list */
struct VNode_t {
	// Properties
	char path[N_NAME];
	stat_t vstat;
	int refcount;
	
	// store a pointer to any extra needed data
	void *extra; 

	// links for list
	VNode previous;
	VNode next;

	// File System Calls
	void (*open)(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
			int *rval, void (*open_done)(L4_ThreadId_t tid, VNode self,
				const char *path, fmode_t mode, int *rval));

	void (*close)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
			int *rval, void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
				int *rval));

	void (*read)(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
			char *buf, size_t nbyte, int *rval, void (*read_done)(L4_ThreadId_t tid,
				VNode self, fildes_t file, L4_Word_t pos, char *buf, size_t nbyte, int *rval));

	void (*write)(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
			const char *buf, size_t nbyte, int *rval, void (*write_done)(L4_ThreadId_t tid,
				VNode self, fildes_t file, L4_Word_t offset, const char *buf, size_t nbyte,
				int *rval));

	void (*getdirent)(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte,
			int *rval);

	void (*stat)(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf, int *rval);

	void (*remove)(L4_ThreadId_t tid, VNode self, const char *path, int *rval);
};

/* System call implementations */
/* See sos.h for more detail */
void vfs_init(void);

/* Open a file, in some cases this just involves increasing a refcount while in others
 * a filesystem must be invoked to handle the call.
 */
void vfs_open(L4_ThreadId_t tid, const char *path, fmode_t mode, int *rval);

/* Close a file in some cases this just involves increasing a refcount while in others
 * a filesystem must be invoked to handle the call, when refcount is zero, the file handler
 * is also closed.
 */
void vfs_close(L4_ThreadId_t tid, fildes_t file, int *rval);

/* Read from a file */
void vfs_read(L4_ThreadId_t tid, fildes_t file, char *buf, size_t nbyte, int *rval);

/* Write to a file */
void vfs_write(L4_ThreadId_t tid, fildes_t file, const char *buf, size_t nbyte, int *rval);

/* Get a directory listing */
void vfs_getdirent(L4_ThreadId_t tid, int pos, char *name, size_t nbyte, int *rval);

/* Stat a file */
void vfs_stat(L4_ThreadId_t tid, const char *path, stat_t *buf, int *rval);

/* Remove a file */
void vfs_remove(L4_ThreadId_t tid, const char *path, int *rval);

#endif

