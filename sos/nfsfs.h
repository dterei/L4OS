#ifndef _NFSFS_H
#define _NFSFS_H

#include <sos/sos.h>
#include <nfs/rpc.h>

#include "vfs.h"

typedef struct NFS_File_t NFS_File;

struct NFS_File_t {
	VNode vnode;
	struct cookie fh;

	NFS_File *previous;
	NFS_File *next;

	uintptr_t lookup;
	L4_ThreadId_t lookup_tid;
	int *rval;
};

int nfsfs_init(void);

void nfsfs_open(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		int *rval, void (*open_done)(L4_ThreadId_t tid, VNode self,
			const char *path, fmode_t mode, int *rval));

void nfsfs_close(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
		int *rval, void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file,
			fmode_t mode, int *rval));

void nfsfs_read(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, int *rval);

void nfsfs_write(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int *rval);

void nfsfs_getdirent(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte,
		int *rval);

void nfsfs_stat(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf, int *rval);

#endif

