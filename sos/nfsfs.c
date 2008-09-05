#include <nfs/nfs.h>

#include "nfsfs.h"
#include "l4.h"
#include "libsos.h"
#include "network.h"

#define verbose 1

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

int
nfsfs_init(void) {
	dprintf(1, "*** nfsfs_init\n");
	// TODO: Move NFS init here instead of network.c
	
#if 0
	IP4_ADDR(&ip_nfsd, 192, 168, 168, 1);
	int r = nfs_init(ip_nfsd);

	if (r != 0) {
		dprintf(0, "!!! nfs_init: Can't initialise nfs (%d) !!!\n", r);
		return (-1);
	}

	if (verbose > 1) {
		mnt_get_export_list();
	}
#endif

	(void) sos_thread_new(&nfsfs_timeout_thread, &nfsfs_timer_stack[STACK_SIZE]);

	return 0;
}

VNode
nfsfs_findvnode(L4_ThreadId_t tid, VNode self, const char *path,
		fmode_t mode, int *rval) {
	dprintf(1, "*** nfsfs_findvnode: %p, %s", self, path);
	return self;
}

void
nfsfs_open(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		int *rval, void (*open_done)(L4_ThreadId_t tid, VNode self,
			const char *path, fmode_t mode, int *rval, VNode vnode)) {
	dprintf(1, "*** nfsfs_open: %p, %s, %d, %p\n", self, path, mode, open_done);
}

void
nfsfs_close(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
		int *rval, void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
			int *rval)) {
	dprintf(1, "*** nfsfs_close: %p, %d, %d, %p\n", self, file, mode, close_done);
}

void
nfsfs_read(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, int *rval) {
	dprintf(1, "*** nfsfs_read: %p, %d, %d, %p, %d\n", self, file, pos, buf, nbyte);
}

void
nfsfs_write(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int *rval) {
	dprintf(1, "*** nfsfs_write: %p, %d, %d, %p, %d\n", self, file, offset, buf, nbyte);
}

void
nfsfs_getdirent(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte,
		int *rval) {
	dprintf(1, "*** nfsfs_getdirent: %p, %d, %s, %d\n", self, pos, name, nbyte);
}

void
nfsfs_stat(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf, int *rval) {
	dprintf(1, "*** nfsfs_write: %p, %s, %p\n", self, path, buf);
}

