#ifndef _VFS_H
#define _VFS_H

#include <sos/sos.h>

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
	void (*open)(pid_t pid, VNode self, const char *path, fmode_t mode,
			void (*open_done)(pid_t pid, VNode self, fmode_t mode, int status));

	void (*close)(pid_t pid, VNode self, fildes_t file, fmode_t mode,
			void (*close_done)(pid_t pid, VNode self, fildes_t file, fmode_t mode, int status));

	void (*read)(pid_t pid, VNode self, fildes_t file, L4_Word_t pos,
			char *buf, size_t nbyte, void (*read_done)(pid_t pid, VNode self,
				fildes_t file, L4_Word_t pos, char *buf, size_t nbyte, int status));

	void (*write)(pid_t pid, VNode self, fildes_t file, L4_Word_t offset,
			const char *buf, size_t nbyte, void (*write_done)(pid_t pid, VNode self,
				fildes_t file, L4_Word_t offset, const char *buf, size_t nbyte, int status));

	void (*flush)(pid_t pid, VNode self, fildes_t file);

	void (*getdirent)(pid_t pid, VNode self, int pos, char *name, size_t nbyte);

	void (*stat)(pid_t pid, VNode self, const char *path, stat_t *buf);

	void (*remove)(pid_t pid, VNode self, const char *path);
};

/* For the PCB */
typedef struct {
	VNode vnode;
	fmode_t fmode;
	L4_Word_t fp;
} VFile;

#define STDOUT_FN "console"
#define STDIN_FN "console"

/* System call implementations */
/* See sos.h for more detail */
void vfs_init(void);

/* Initialise an array of fildes, must be size PROCESS_MAX_FDS */
/* Initialise an array of vfiles, must be size PROCESS_MAX_FILES */
void vfiles_init(fildes_t *fds, VFile *files);

/* Open a file, in some cases this just involves increasing a refcount while in others
 * a filesystem must be invoked to handle the call.
 */
void vfs_open(pid_t pid, const char *path, fmode_t mode,
		unsigned int readers, unsigned int writers);

/* Close a file in some cases this just involves increasing a refcount while in others
 * a filesystem must be invoked to handle the call, when refcount is zero, the file handler
 * is also closed.
 */
void vfs_close(pid_t pid, fildes_t file);

/* Read from a file */
void vfs_read(pid_t pid, fildes_t file, char *buf, size_t nbyte);

/* Write to a file */
void vfs_write(pid_t pid, fildes_t file, const char *buf, size_t nbyte);

/* Flush a stream */
void vfs_flush(pid_t pid, fildes_t file);

/* Seek to a position in a file */
void vfs_lseek(pid_t pid, fildes_t file, fpos_t pos, int whence);

/* Get a directory listing */
void vfs_getdirent(pid_t pid, int pos, char *name, size_t nbyte);

/* Stat a file */
void vfs_stat(pid_t pid, const char *path, stat_t *buf);

/* Remove a file */
void vfs_remove(pid_t pid, const char *path);

/* Duplicate the given file descriptor to the one specified */
void vfs_dup(pid_t pid, fildes_t forig, fildes_t fdup);

/* Test is a file is open (internal SOS function) */
int vfs_isopen(VFile *file);

#endif // sos/vfs.h
