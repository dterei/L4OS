#ifndef _VFS_H
#define _VFS_H

#include <sos/sos.h>

/* Simple VFS-style vnode */
typedef struct VNode_t *VNode;

/* All allocated vnodes stored in double link list */
struct VNode_t {
	// Properties
	char *path;
	stat_t vstat;
	int refcount;
	
	// store a pointer to any extra needed data
	void *extra; 

	// links for list
	VNode previous;
	VNode next;

	// Callbacks
	void (*open)(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
			int *rval, void (*open_done)(L4_ThreadId_t tid, VNode self,
				const char *path, fmode_t mode, int *rval));

	void (*close)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
			int *rval, void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
				int *rval));

	void (*read)(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
			char *buf, size_t nbyte, int *rval);

	void (*write)(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
			const char *buf, size_t nbyte, int *rval);

	void (*getdirent)(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte,
			int *rval);

	void (*stat)(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf, int *rval);
};

/* Global VNode list */
extern VNode GlobalVNodes;

/*
 * TODO have a PCB rather than this.  Not all that challenging and
 * probably cleaner when there is more complex stuff to store
 * with each address space.
 */
typedef struct {
	VNode vnode;
	/* This stores the permissions the file was opened with while the fmode_t in
	   the vnode stores the permission stored with the file on disk */
	fmode_t fmode;
	// TODO: Should there be one for read and one for write?
	L4_Word_t fp;
} VFile_t;

/* Per process open file table */
extern VFile_t openfiles[MAX_ADDRSPACES][PROCESS_MAX_FILES];

/* Global record of the "special files" */
extern VNode specialFiles;

/* System call implementations */
void vfs_init(void);

void vfs_open(L4_ThreadId_t tid, const char *path, fmode_t mode, int *rval);
void vfs_open_done(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode, int *rval);

void vfs_close(L4_ThreadId_t tid, fildes_t file, int *rval);
void vfs_close_done(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode, int *rval);

void vfs_read(L4_ThreadId_t tid, fildes_t file, char *buf, size_t nbyte, int *rval);

void vfs_write(L4_ThreadId_t tid, fildes_t file, const char *buf, size_t nbyte, int *rval);

void vfs_getdirent(L4_ThreadId_t tid, int pos, char *name, size_t nbyte, int *rval);

void vfs_stat(L4_ThreadId_t tid, const char *path, stat_t *buf, int *rval);

/* Other functions */
fildes_t findNextFd(int spaceId);

#endif

