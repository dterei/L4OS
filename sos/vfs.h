#ifndef _VFS_H
#define _VFS_H

#include <sos/sos.h>

/* Simple VFS-style vnode */
typedef struct VNode_t *VNode;

struct VNode_t {
	// Properties
	char *path;
	stat_t stat;
	void *extra; // store a pointer to any extra needed data

	// Callbacks
	int (*open)(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode);

	int (*close)(L4_ThreadId_t tid, VNode self, fildes_t file);

	int (*read)(L4_ThreadId_t tid, VNode self, fildes_t file,
			char *buf, size_t nbyte);

	int (*write)(L4_ThreadId_t tid, VNode self, fildes_t file,
			const char *buf, size_t nbyte);
};

/* Global record of the "special files" */
typedef struct SpecialFile_t *SpecialFile;
struct SpecialFile_t {
	VNode file;
	SpecialFile next;
};

extern SpecialFile specialFiles;

/* All allocated vnodes */
extern VNode vnodes[MAX_ADDRSPACES][PROCESS_MAX_FILES];

/* System call implementations */
void vfs_init(void);
fildes_t vfs_open(L4_ThreadId_t tid, const char *path, fmode_t mode);
int vfs_close(L4_ThreadId_t tid, fildes_t file);
int vfs_read(L4_ThreadId_t tid, fildes_t file, char *buf, size_t nbyte);
int vfs_write(L4_ThreadId_t tid, fildes_t file, const char *buf, size_t nbyte);

/* Other functions */
fildes_t findNextFd(int spaceId);

#endif

