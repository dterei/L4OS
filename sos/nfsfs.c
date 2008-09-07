#include <nfs/nfs.h>

#include "nfsfs.h"

#include "l4.h"
#include "libsos.h"
#include "network.h"
#include "syscall.h"

#define verbose 2

/*** NFS TIMEOUT THREAD ***/
extern void nfs_timeout(void);

// stack for nfs timer thread, can probably be a lot less
#define STACK_SIZE 0x1000
static L4_Word_t nfsfs_timer_stack[STACK_SIZE];

#define MS_TO_US 1000
#define NFSFS_TIMEOUT_MS (100 * MS_TO_US)

static
void
nfsfs_timeout_thread(void) {
	while (1) {
		sos_usleep(NFSFS_TIMEOUT_MS);
		nfs_timeout();
		dprintf(2, "*** nfs_timeout_thread: timout event!\n");
	}
}

/*** NFS FS ***/
#define NULL_TOKEN ((uintptr_t) (-1))

NFS_File *NfsFiles;

int
nfsfs_init(void) {
	dprintf(1, "*** nfsfs_init\n");
	// TODO: Move NFS init here instead of network.c
	NfsFiles = NULL;
	(void) sos_thread_new(&nfsfs_timeout_thread, &nfsfs_timer_stack[STACK_SIZE]);
	return 0;
}

static
uintptr_t
newtoken(void) {
	static uintptr_t tok = 0;
	tok++;
	return (tok % ( NULL_TOKEN - 1));
}

static
NFS_File *
createfile(void) {
	NFS_File *nf = (NFS_File *) malloc(sizeof(NFS_File));
	nf->vnode = NULL;
	//nf->fh.data = NULL;
	nf->lookup = NULL_TOKEN;
	nf->lookup_tid = L4_nilthread;
	return nf;
}

static
void
addfile(NFS_File *new_nf) {
	new_nf->next = NfsFiles;
	new_nf->previous = NULL;
	NfsFiles->previous = new_nf;
	NfsFiles = new_nf;
}

static
void 
lookup_cb(uintptr_t token, int status, struct cookie *fh, fattr_t *attr) {
	dprintf(1, "*** nfsfs_lookup_cb: %d, %d, %p, %p\n", token, status, fh, attr);

	NFS_File *nf = NULL;
	for (nf = NfsFiles; nf != NULL; nf = nf->next) {
		dprintf(1, "nfsfs_lookup_cb: NFS_File: %d\n", nf->lookup);
		if (nf->lookup == token) break;
	}

	if (nf == NULL) {
		dprintf(0, "Corrupt callback, no matching token: %d\n", token);
	} else {
		// need to copy token and attr.	
		dprintf(1, "Sending: %d, %d\n", token, L4_ThreadNo(nf->lookup_tid));
		*(nf->rval) = (-1);
		syscall_reply(nf->lookup_tid);
	}

}

void
nfsfs_open(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		int *rval, void (*open_done)(L4_ThreadId_t tid, VNode self,
			const char *path, fmode_t mode, int *rval)) {
	dprintf(1, "*** nfsfs_open: %p, %s, %d, %p\n", self, path, mode, open_done);

	NFS_File *nf = createfile();
	addfile(nf);
	nf->lookup = newtoken();
	nf->lookup_tid = tid;
	nf->rval = rval;

	char *path2 = (char *) path;
	nfs_lookup(&mnt_point, path2, lookup_cb, nf->lookup);
}

void
nfsfs_close(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
		int *rval, void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
			int *rval)) {
	dprintf(1, "*** nfsfs_close: %p, %d, %d, %p\n", self, file, mode, close_done);

	*rval = -1;
	syscall_reply(tid);
}

void
nfsfs_read(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, int *rval) {
	dprintf(1, "*** nfsfs_read: %p, %d, %d, %p, %d\n", self, file, pos, buf, nbyte);

	*rval = -1;
	syscall_reply(tid);
}

void
nfsfs_write(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int *rval) {
	dprintf(1, "*** nfsfs_write: %p, %d, %d, %p, %d\n", self, file, offset, buf, nbyte);

	*rval = -1;
	syscall_reply(tid);
}

void
nfsfs_getdirent(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte,
		int *rval) {
	dprintf(1, "*** nfsfs_getdirent: %p, %d, %s, %d\n", self, pos, name, nbyte);

	*rval = -1;
	syscall_reply(tid);
}

void
nfsfs_stat(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf, int *rval) {
	dprintf(1, "*** nfsfs_write: %p, %s, %p\n", self, path, buf);

	*rval = -1;
	syscall_reply(tid);
}

