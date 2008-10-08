#ifndef _NFSFS_H
#define _NFSFS_H

#include <sos/sos.h>

#include "vfs.h"

/*
 * Wrappers for nfs open/read/write operations.
 * See libs/sos/include/sos.h for what they do.
 */

/* Start up NFS file system */
int nfsfs_init(void);

/* Open a specified file using NFS */
void nfsfs_open(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		void (*open_done)(L4_ThreadId_t tid, VNode self, fmode_t mode, int status));

/* Close a specified file previously opened with nfsfs_open */
void nfsfs_close(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
		void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file,
			fmode_t mode, int status));

/* Close a specified file previously opened with nfsfs_open */
void nfsfs_read(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, void (*read_done)(L4_ThreadId_t tid,
			VNode self, fildes_t file, L4_Word_t pos, char *buf, size_t nbyte, int status));

/* Write the specified number of bytes from the buffer buf to a given NFS file */
void nfsfs_write(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, void (*write_done)(L4_ThreadId_t tid, VNode self,
			fildes_t file, L4_Word_t offset, const char *buf, size_t nbyte, int status));

/* Get directory entries of the NFS filesystem */
void nfsfs_getdirent(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte);

/* Get file details for a specified NFS File */
void nfsfs_stat(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf);

/* Remove a file */
void nfsfs_remove(L4_ThreadId_t tid, VNode self, const char *path);

#endif // sos/nfsfs.h
