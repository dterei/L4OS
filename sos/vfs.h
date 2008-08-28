#ifndef _VFS_H
#define _VFS_H

#include <sos/sos.h>

/* Simple VFS-style vnode */
typedef struct VNode_t {
	// Properties
	char *path;
	stat_t stat;

	// Callbacks
	int (*open)(const char *path, fmode_t mode);
	int (*close)(fildes_t file);
	int (*read)(fildes_t file, char *buf, size_t nbyte);
	int (*write)(fildes_t file, const char *buf, size_t nbyte);
} *VNode;

/* Global record of the "special files" */
typedef struct SpecialFile_t *SpecialFile;
struct SpecialFiles_t {
	VNode file;
	SpecialFile next;
};

extern SpecialFile specialFiles;

/* All allocated vnodes */
extern VNode vnodes[MAX_ADDRSPACES][PROCESS_MAX_FILES];

/* System call implementations */
void vfs_init(void);
fildes_t vfs_open(const char *path, fmode_t mode);
int vfs_close(fildes_t file);
int vfs_read(fildes_t file, char *buf, size_t nbyte);
int vfs_write(fildes_t file, const char *buf, size_t nbyte);

#endif
