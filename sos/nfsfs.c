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

	return 1;
}

fildes_t
nfsfs_open(L4_ThreadId_t tid, VNode self, const char *path,
		fmode_t mode) {
	//TODO
	return (-1);
}

int
nfsfs_close(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode) {
	//TODO
	return (-1);
}

void
nfsfs_read(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, int *rval) {
	//TODO
}

void
nfsfs_write(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int *rval) {
	//TODO
}

