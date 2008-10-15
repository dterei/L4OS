#ifndef _VFS_H
#define _VFS_H

#include <sos/sos.h>

#define VFS_UNLIMITED_RW ((unsigned int) (-1))
#define VFS_NIL_FILE (-1)

/* Simple VFS-style vnode */
typedef struct VNode_t *VNode;

/* All allocated vnodes stored in double link list */
struct VNode_t {
	// Properties
	char path[MAX_FILE_NAME];
	stat_t vstat;

	// Open counters
	unsigned int Max_Readers;
	unsigned int Max_Writers;
	unsigned int readers;
	unsigned int writers;
	
	// store a pointer to any extra needed data
	void *extra; 

	// links for list
	VNode previous;
	VNode next;

	// File System Calls
	void (*open)(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
			void (*open_done)(L4_ThreadId_t tid, VNode self, fmode_t mode, int status));

	void (*close)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
			void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode, int status));

	void (*read)(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
			char *buf, size_t nbyte, void (*read_done)(L4_ThreadId_t tid, VNode self,
				fildes_t file, L4_Word_t pos, char *buf, size_t nbyte, int status));

	void (*write)(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
			const char *buf, size_t nbyte, void (*write_done)(L4_ThreadId_t tid, VNode self,
				fildes_t file, L4_Word_t offset, const char *buf, size_t nbyte, int status));

	void (*flush)(L4_ThreadId_t tid, VNode self, fildes_t file);

	void (*getdirent)(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte);

	void (*stat)(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf);

	void (*remove)(L4_ThreadId_t tid, VNode self, const char *path);
};

/* For the PCB */
typedef struct VFile_t VFile;

struct VFile_t {
	VNode vnode;
	fmode_t fmode;
	L4_Word_t fp;
};

/* System call implementations */
/* See sos.h for more detail */
void vfs_init(void);

/* Initialise an array of vfiles, must be size PROCESS_MAX_FILES */
void vfiles_init(VFile *files);

/* Open a file, in some cases this just involves increasing a refcount while in others
 * a filesystem must be invoked to handle the call.
 */
void vfs_open(L4_ThreadId_t tid, const char *path, fmode_t mode);

/* Close a file in some cases this just involves increasing a refcount while in others
 * a filesystem must be invoked to handle the call, when refcount is zero, the file handler
 * is also closed.
 */
void vfs_close(L4_ThreadId_t tid, fildes_t file);

/* Read from a file */
void vfs_read(L4_ThreadId_t tid, fildes_t file, char *buf, size_t nbyte);

/* Write to a file */
void vfs_write(L4_ThreadId_t tid, fildes_t file, const char *buf, size_t nbyte);

/* Flush a stream */
void vfs_flush(L4_ThreadId_t tid, fildes_t file);

/* Seek to a position in a file */
void vfs_lseek(L4_ThreadId_t tid, fildes_t file, fpos_t pos, int whence);

/* Get a directory listing */
void vfs_getdirent(L4_ThreadId_t tid, int pos, char *name, size_t nbyte);

/* Stat a file */
void vfs_stat(L4_ThreadId_t tid, const char *path, stat_t *buf);

/* Remove a file */
void vfs_remove(L4_ThreadId_t tid, const char *path);

#endif // sos/vfs.h
