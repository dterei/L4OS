#include <string.h>
#include <nfs/nfs.h>

#include "nfsfs.h"

#include "l4.h"
#include "libsos.h"
#include "network.h"
#include "syscall.h"

#define verbose 3

/*** NFS TIMEOUT THREAD ***/
extern void nfs_timeout(void);

// stack for nfs timer thread, can probably be a lot less
// TODO: Have better kernel stacks 
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
#define UDP_PAYLOAD 500
#define NULL_TOKEN ((uintptr_t) (-1))

NFS_BaseRequest *NfsRequests;

int
nfsfs_init(void) {
	dprintf(1, "*** nfsfs_init\n");
	// TODO: Move NFS init here instead of network.c
	NfsRequests = NULL;
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
NFS_BaseRequest *
create_request(enum NfsRequestType rt) {
	NFS_BaseRequest *rq;
	switch (rt) {
		case RT_LOOKUP:
			rq = (NFS_BaseRequest *) malloc(sizeof(NFS_LookupRequest));
			rq->rt = RT_LOOKUP;
			break;
		case RT_READ:
			rq = (NFS_BaseRequest *) malloc(sizeof(NFS_ReadRequest));
			rq->rt = RT_READ;
			break;
		case RT_WRITE:
			rq = (NFS_BaseRequest *) malloc(sizeof(NFS_WriteRequest));
			rq->rt = RT_WRITE;
			break;
		default:
			return NULL;
			dprintf(0, "nfsfs.c: create_request: invalid request type %d\n", rt);
	}

	rq->token = newtoken();

	// handle empty list
	if (NfsRequests == NULL) {
		NfsRequests = rq;
	} else {
		rq->next = NfsRequests;
		rq->previous = NULL;
		NfsRequests->previous = rq;
		NfsRequests = rq;
	}

	return rq;
}

static
void
remove_request(NFS_BaseRequest *rq) {
	// remove from lists
	NFS_BaseRequest *prq, *nrq;
	prq = rq->previous;
	nrq = rq->next;

	// handle empty list
	if (prq == NULL && nrq == NULL) {
		NfsRequests = NULL;
	}
	// handle end of list
	else if (nrq == NULL) {
		prq->next = NULL;
	}
	// handle start of list
	else if (prq == NULL) {
		NfsRequests = nrq;
		nrq->previous = NULL;
	}
	// handle other usual case
	else {
		prq->next = nrq;
		nrq->previous = prq;
	}

	// free memory
	switch (rq->rt) {
		case RT_LOOKUP:
			free((NFS_LookupRequest *) rq);
			break;
		case RT_READ:
			free((NFS_ReadRequest *) rq);
			break;
		case RT_WRITE:
			free((NFS_WriteRequest *) rq);
			break;
		default:
			dprintf(0, "nfsfs.c: remove_request: invalid request type %d\n", rq->rt);
	}
}

static
void 
lookup_cb(uintptr_t token, int status, struct cookie *fh, fattr_t *attr) {
	dprintf(1, "*** nfsfs_lookup_cb: %d, %d, %p, %p\n", token, status, fh, attr);

	NFS_LookupRequest *rq = NULL;
	for (NFS_BaseRequest* brq = NfsRequests; brq != NULL; brq = brq->next) {
		dprintf(1, "nfsfs_lookup_cb: NFS_BaseRequest: %d\n", brq->token);
		if (brq->token == token) {
			rq = (NFS_LookupRequest *) brq;
		}
	}

	if (rq == NULL) {
		dprintf(0, "!!!nfsfs: Corrupt lookup callback, no matching token: %d\n", token);
		return;
	}

	if (status != NFS_OK) {
		dprintf(0, "!!!nfsfs: lookup_cb: Error occured! (%d)\n", status);
		free((NFS_File *) rq->vnode->extra);
		free(rq->vnode);
		(*rq->rval) = (-1);
		syscall_reply(rq->tid);
		remove_request((NFS_BaseRequest *) rq);
		return;
	}

	NFS_File *nf = (NFS_File *) rq->vnode->extra;
	memcpy((void *) &(nf->fh), (void *) fh, sizeof(struct cookie));
	memcpy((void *) &(nf->attr), (void *) attr, sizeof(fattr_t));

	dprintf(1, "Sending: %d, %d\n", token, L4_ThreadNo(rq->tid));
	*(rq->rval) = 0;
	rq->open_done(rq->tid, rq->vnode, rq->vnode->path, rq->mode, rq->rval);
	syscall_reply(rq->tid);
	remove_request((NFS_BaseRequest *) rq);
}

static
void
getvnode(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		int *rval, void (*open_done)(L4_ThreadId_t tid, VNode self,
			const char *path, fmode_t mode, int *rval)) {
	dprintf(1, "*** nfsfs_getvnode: %p, %s, %d, %p\n", self, path, mode, open_done);

	self = (VNode) malloc(sizeof(struct VNode_t));
	if (self == NULL) {
		// TODO: handle fail of malloc
	}
	strncpy(self->path, path, N_NAME);
	self->refcount = 1;

	NFS_File *nf = (NFS_File *) malloc(sizeof(NFS_File));
	nf->vnode = self;
	self->extra = (void *) nf;
	self->open = nfsfs_open;
	self->close = nfsfs_close;
	self->read = nfsfs_read;
	self->write = nfsfs_write;
	self->getdirent = nfsfs_getdirent;
	self->stat = nfsfs_stat;

	NFS_LookupRequest *rq = (NFS_LookupRequest *) create_request(RT_LOOKUP);

	rq->vnode = self;
	rq->tid = tid;
	rq->mode = mode;
	rq->rval = rval;
	rq->open_done = open_done;

	char *path2 = (char *) path;
	nfs_lookup(&mnt_point, path2, lookup_cb, rq->token);
}

void
nfsfs_open(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		int *rval, void (*open_done)(L4_ThreadId_t tid, VNode self,
			const char *path, fmode_t mode, int *rval)) {
	dprintf(1, "*** nfsfs_open: %p, %s, %d, %p\n", self, path, mode, open_done);

	if (self == NULL) {
		dprintf(2, "nfs_open: get new vnode\n");
		getvnode(tid, self, path, mode, rval, open_done);
	} else {
		dprintf(2, "nfs_open: already open vnode, increase refcount\n\n");
		self->refcount++;
		*rval = 0;
		open_done(tid, self, path, mode, rval);
		syscall_reply(tid);
	}
};

void
nfsfs_close(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
		int *rval, void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
			int *rval)) {
	dprintf(1, "*** nfsfs_close: %p, %d, %d, %p\n", self, file, mode, close_done);

	*rval = (-1);
	syscall_reply(tid);
}

static
void 
read_cb(uintptr_t token, int status, fattr_t *attr, int bytes_read, char *data) {
	dprintf(1, "*** nfsfs_read_cb: %u, %d, %d, %p\n", token, status, bytes_read, data);

	// TODO: Can probably move this code to a generic function
	NFS_ReadRequest *rq = NULL;
	for (NFS_BaseRequest* brq = NfsRequests; brq != NULL; brq = brq->next) {
		dprintf(2, "nfsfs_read_cb: NFS_BaseRequest: %d\n", brq->token);
		if (brq->token == token) {
			rq = (NFS_ReadRequest *) brq;
		}
	}

	if (rq == NULL) {
		dprintf(0, "!!!nfsfs: Corrupt read callback, no matching token: %d\n", token);
		return;
	}

	if (status != NFS_OK) {
		free((NFS_File *) rq->vnode->extra);
		free(rq->vnode);
		(*rq->rval) = (-1);
		rq->read_done(rq->tid, rq->vnode, rq->file, 0, rq->buf, 0, rq->rval);
		syscall_reply(rq->tid);
		remove_request((NFS_BaseRequest *) rq);
		return;
	}

	NFS_File *nf = (NFS_File *) rq->vnode->extra;

	memcpy((void *) &(nf->attr), (void *) attr, sizeof(fattr_t));
	strncpy(rq->buf, data, bytes_read);
	*(rq->rval) = bytes_read;

	// call vfs to handle fp and anything else
	rq->read_done(rq->tid, rq->vnode, rq->file, 0, rq->buf, bytes_read, rq->rval);
	
	dprintf(2, "nfsfs: Read CB Sending: %d, %d\n", token, L4_ThreadNo(rq->tid));
	syscall_reply(rq->tid);
	remove_request((NFS_BaseRequest *) rq);
}

void
nfsfs_read(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, int *rval, void (*read_done)(L4_ThreadId_t tid,
			VNode self, fildes_t file, L4_Word_t pos, char *buf, size_t nbyte, int *rval)) {
	dprintf(1, "*** nfsfs_read: %p, %d, %d, %p, %d\n", self, file, pos, buf, nbyte);

	NFS_File *nf = (NFS_File *) self->extra;	
	if (nf == NULL) {
		dprintf(0, "!!! nfsfs_read: Invalid NFS file (p %d, f %d), no nfs struct!\n", L4_ThreadNo(tid), file);
		*rval = (-1);
		syscall_reply(tid);
		return;
	}

	if (nbyte >= UDP_PAYLOAD) {
		nbyte = UDP_PAYLOAD - 1;
	}

	NFS_ReadRequest *rq = (NFS_ReadRequest *) create_request(RT_READ);
	rq->vnode = self;
	rq->tid = tid;
	rq->file = file;
	rq->buf = buf;
	rq->rval = rval;
	rq->read_done = read_done;

	dprintf(2, "nfsfs: nfs_read call (token %u)\n", rq->token);
	nfs_read(&(nf->fh), pos, nbyte, read_cb, rq->token);
}

static
void
write_cb(uintptr_t token, int status, fattr_t *attr) {
	dprintf(1, "*** nfsfs_write_cb: %u, %d, %p\n", token, status, attr);

	// TODO: Can probably move this code to a generic function
	NFS_WriteRequest *rq = NULL;
	for (NFS_BaseRequest* brq = NfsRequests; brq != NULL; brq = brq->next) {
		dprintf(2, "nfsfs_write_cb: NFS_BaseRequest: %d\n", brq->token);
		if (brq->token == token) {
			rq = (NFS_WriteRequest *) brq;
		}
	}

	if (rq == NULL) {
		dprintf(0, "!!!nfsfs: Corrupt write callback, no matching token: %d\n", token);
		return;
	}

	if (status != NFS_OK) {
		free((NFS_File *) rq->vnode->extra);
		free(rq->vnode);
		(*rq->rval) = (-1);
		rq->write_done(rq->tid, rq->vnode, rq->file, 0, rq->buf, 0, rq->rval);
		syscall_reply(rq->tid);
		remove_request((NFS_BaseRequest *) rq);
		return;
	}

	// call vfs to handle fp and anything else
	rq->write_done(rq->tid, rq->vnode, rq->file, 0, rq->buf, (size_t) *(rq->rval), rq->rval);
	
	dprintf(2, "nfsfs: Write CB Sending: %d, %d\n", token, L4_ThreadNo(rq->tid));
	syscall_reply(rq->tid);
	remove_request((NFS_BaseRequest *) rq);
}

void
nfsfs_write(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int *rval, void (*write_done)(L4_ThreadId_t tid,
			VNode self, fildes_t file, L4_Word_t offset, const char *buf, size_t nbyte,
			int *rval)) {
	dprintf(1, "*** nfsfs_write: %p, %d, %d, %p, %d\n", self, file, offset, buf, nbyte);

	NFS_File *nf = (NFS_File *) self->extra;	
	if (nf == NULL) {
		dprintf(0, "!!! nfsfs_write: Invalid NFS file (p %d, f %d), no nfs struct!\n", L4_ThreadNo(tid), file);
		*rval = (-1);
		syscall_reply(tid);
		return;
	}

	// TODO: Should be able to handle arbitary size, perhaps just change libc to loop.
	if (nbyte >= UDP_PAYLOAD) {
		dprintf(0, "!!! nfsfs_write: Write request size too large! (tid %d) (file %d) (size %d) (max %d)!\n",
				L4_ThreadNo(tid), file, nbyte, UDP_PAYLOAD);
		*rval = (-1);
		syscall_reply(tid);
		return;
	}

	NFS_WriteRequest *rq = (NFS_WriteRequest *) create_request(RT_WRITE);
	rq->vnode = self;
	rq->tid = tid;
	rq->file = file;
	rq->buf = (char *) buf;
	rq->rval = rval;
	rq->write_done = write_done;

	// Set rval to nbyte already, will reset if error occurs in write_cb
	*rval = nbyte;

	dprintf(2, "nfsfs: nfs_write call (token %u)\n", rq->token);
	nfs_write(&(nf->fh), offset, nbyte, rq->buf, write_cb, rq->token);
}

void
nfsfs_getdirent(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte,
		int *rval) {
	dprintf(1, "*** nfsfs_getdirent: %p, %d, %s, %d\n", self, pos, name, nbyte);

	*rval = (-1);
	syscall_reply(tid);
}

void
nfsfs_stat(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf, int *rval) {
	dprintf(1, "*** nfsfs_write: %p, %s, %p\n", self, path, buf);

	*rval = (-1);
	syscall_reply(tid);
}

