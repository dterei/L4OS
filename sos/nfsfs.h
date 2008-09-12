#ifndef _NFSFS_H
#define _NFSFS_H

#include <sos/sos.h>
#include <nfs/rpc.h>

#include "vfs.h"

/* NFS File */
typedef struct {
	VNode vnode;
	struct cookie fh;
	fattr_t attr;
} NFS_File;

/* Types of NFS request, used for continuations until callbacks */
enum NfsRequestType {
	RT_LOOKUP, /* aka OPEN */
	RT_READ,
	RT_WRITE,
	RT_STAT
};

typedef struct NFS_BaseRequest_t NFS_BaseRequest;

/* Base NFS request object.
 * Make sure all other request implement the same elements of this structure
 * at the start so we can treat them as this base class.
 */
struct NFS_BaseRequest_t {
	enum NfsRequestType rt;
	uintptr_t token;
	VNode vnode;
	L4_ThreadId_t tid;

	NFS_BaseRequest *previous;
	NFS_BaseRequest *next;
};

typedef struct {
	enum NfsRequestType rt;
	uintptr_t token;
	VNode vnode;
	L4_ThreadId_t tid;

	NFS_BaseRequest *previous;
	NFS_BaseRequest *next;

	fmode_t mode;
	int *rval;

	void (*open_done) (L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode, int *rval);
} NFS_LookupRequest;

/* Could combine read and write since are the same, but prefer separate for
 * easy extension
 */
typedef struct {
	enum NfsRequestType rt;
	uintptr_t token;
	VNode vnode;
	L4_ThreadId_t tid;

	NFS_BaseRequest *previous;
	NFS_BaseRequest *next;

	fildes_t file;
	char *buf;
	int *rval;

	void (*read_done)(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos, char *buf,
			size_t nbyte, int *rval);
} NFS_ReadRequest;

typedef struct {
	enum NfsRequestType rt;
	uintptr_t token;
	VNode vnode;
	L4_ThreadId_t tid;

	NFS_BaseRequest *previous;
	NFS_BaseRequest *next;

	fildes_t file;
	char *buf;
	int *rval;

	void (*write_done)(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
			const char *buf, size_t nbyte, int *rval);
} NFS_WriteRequest;

typedef struct {
	enum NfsRequestType rt;
	uintptr_t token;
	VNode vnode;
	L4_ThreadId_t tid;

	NFS_BaseRequest *previous;
	NFS_BaseRequest *next;

	stat_t *stat;
	int *rval;
} NFS_StatRequest;

struct NFS_DirRequest_t {
	enum NfsRequestType rt;
	uintptr_t token;
	VNode vnode;
	L4_ThreadId_t tid;

	NFS_BaseRequest *previous;
	NFS_BaseRequest *next;
};

int nfsfs_init(void);

void nfsfs_open(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		int *rval, void (*open_done)(L4_ThreadId_t tid, VNode self,
			const char *path, fmode_t mode, int *rval));

void nfsfs_close(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
		int *rval, void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file,
			fmode_t mode, int *rval));

void nfsfs_read(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, int *rval, void (*read_done)(L4_ThreadId_t tid,
			VNode self, fildes_t file, L4_Word_t pos, char *buf, size_t nbyte, int *rval));

void nfsfs_write(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int *rval, void (*write_done)(L4_ThreadId_t tid,
			VNode self, fildes_t file, L4_Word_t offset, const char *buf, size_t nbyte,
			int *rval));

void nfsfs_getdirent(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte,
		int *rval);

void nfsfs_stat(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf, int *rval);

#endif

