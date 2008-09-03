#ifndef _VFS_H
#define _VFS_H

#include <sos/sos.h>

/* Simple VFS-style vnode */
typedef struct VNode_t *VNode;

/* All allocated vnodes stored in double link list */
struct VNode_t {
	// Properties
	char *path;
	stat_t stat;
	int refcount;
	
	// store a pointer to any extra needed data
	void *extra; 

	// links for list
	VNode previous;
	VNode next;

	// Callbacks
	int (*open)(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode);

	int (*close)(L4_ThreadId_t tid, VNode self, fildes_t file);

	void (*read)(L4_ThreadId_t tid, VNode self, fildes_t file,
			char *buf, size_t nbyte, int *rval);

	void (*write)(L4_ThreadId_t tid, VNode self, fildes_t file,
			const char *buf, size_t nbyte, int *rval);
};


// Global VNode list
extern VNode GlobalVNodes;

/* Per process open file table */
// TODO have a PCB rather than this.  Not all that challenging and
// probably cleaner when there is more complex stuff to store
// with each address space.

typedef struct {
	VNode vnode;
	fmode_t fmode;
	L4_Word_t fp;
} VFile_t;

extern VFile_t openfiles[MAX_ADDRSPACES][PROCESS_MAX_FILES];

/* Global record of the "special files" */
extern VNode specialFiles;

/* System call implementations */
void vfs_init(void);
fildes_t vfs_open(L4_ThreadId_t tid, const char *path, fmode_t mode);
int vfs_close(L4_ThreadId_t tid, fildes_t file);
void vfs_read(L4_ThreadId_t tid, fildes_t file, char *buf, size_t nbyte, int *rval);
void vfs_write(L4_ThreadId_t tid, fildes_t file, const char *buf, size_t nbyte, int *rval);

/* Other functions */
fildes_t findNextFd(int spaceId);

#endif

