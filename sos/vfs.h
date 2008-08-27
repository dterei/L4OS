#ifndef _VFS_H
#define _VFS_H

#include <sos/sos.h>

fildes_t vfs_open(const char *path, fmode_t mode);
int vfs_close(fildes_t file);
int vfs_read(fildes_t file, char *buf, size_t nbyte);
int vfs_write(fildes_t file, const char *buf, size_t nbyte);

#if 0
#include <sos/sos.h>

void vfs_init();

typedef struct GlobalFiles_t *GlobalFiles;
struct GlobalFiles_t {
	char *name;
	int readers;
	int writers;
	int maxReaders; // for console
	GlobalFiles next;
};

GlobalFiles *globalFiles = NULL;
GlobalFiles *specialFiles = NULL;

struct LocalFD {
	fmode_t mode;
	int pos;
	// Need some way of storing NFS info
};

struct LocalFD localFds[MAX_ADDRSPACES][PROCESS_MAX_FILES];
#endif

#endif
