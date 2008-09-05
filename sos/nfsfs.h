#ifndef _NFSFS_H
#define _NFSFS_H

#include <sos/sos.h>

#include "vfs.h"

int nfsfs_init(void);

fildes_t nfsfs_open(L4_ThreadId_t tid, VNode self, const char *path,
		fmode_t mode);

int nfsfs_close(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode);

void nfsfs_read(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, int *rval);

void nfsfs_write(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int *rval);

#endif

