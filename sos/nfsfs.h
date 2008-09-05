#ifndef _NFSFS_H
#define _NFSFS_H

#include <sos/sos.h>

#include "vfs.h"

int nfsfs_init(void);

VNode nfsfs_findvnode(L4_ThreadId_t tid, VNode self, const char *path,
		fmode_t mode, int *rval);

void nfsfs_open(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		int *rval, void (*open_done)(L4_ThreadId_t tid, VNode self,
			const char *path, fmode_t mode, int *rval, VNode vnode));

void nfsfs_close(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
		int *rval, void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
			int *rval));

void nfsfs_read(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, int *rval);

void nfsfs_write(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int *rval);

void nfsfs_getdirent(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte,
		int *rval);

void nfsfs_stat(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf, int *rval);

#endif

