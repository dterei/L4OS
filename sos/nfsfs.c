#include <string.h>
#include <nfs/nfs.h>

#include "nfsfs.h"

#include "l4.h"
#include "libsos.h"
#include "network.h"
#include "syscall.h"

#define verbose 0

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
		dprintf(3, "*** nfs_timeout_thread: timout event!\n");
	}
}

/*** NFS FS ***/
#define NFS_BUFSIZ 500
#define NULL_TOKEN ((uintptr_t) (-1))
/*
 * NFS Permissions.
 * _ | ___ | ___ | ___
 * s u rwx g rwx o rwx
 * 512     ...       1
 * Set default to rw for all.
 */
#define DEFAULT_SATTR { (438), (0), (0), (0), {(0), (0)}, {(0), (0)} }

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
	// TODO: Move inserting vnode, tid... ect up to this function.
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
		case RT_STAT:
			rq = (NFS_BaseRequest *) malloc(sizeof(NFS_StatRequest));
			rq->rt = RT_STAT;
			break;
		case RT_DIR:
			rq = (NFS_BaseRequest *) malloc(sizeof(NFS_DirRequest));
			rq->rt = RT_DIR;
			break;
		default:
			dprintf(0, "!!! nfsfs_create_request: invalid request type %d\n", rt);
			return NULL;
	}

	rq->token = newtoken();

	// make sure not in list alredy
	for (NFS_BaseRequest* brq = NfsRequests; brq != NULL; brq = brq->next) {
		if (brq == rq) {
			dprintf(0, "!!! BaseRequest already in list! %p %p %p %p\n", NfsRequests, brq, brq->next, brq->previous);
			return rq;
		}
	}

	rq->next = NULL;
	rq->previous = NULL;

	// add to list if list not empty
	if (NfsRequests != NULL) {
		rq->next = NfsRequests;
		NfsRequests->previous = rq;
	}

	NfsRequests = rq;

	for (NFS_BaseRequest* brq = NfsRequests; brq != NULL; brq = brq->next) {
		dprintf(1, "&&& Add NFS_BaseRequest: %d, %p, %p, %p\n", brq->token, brq, brq->next, brq->previous);
	}

	return NfsRequests;
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
		case RT_STAT:
			free((NFS_StatRequest *) rq);
			break;
		case RT_DIR:
			free((NFS_DirRequest *) rq);
			break;
		default:
			dprintf(0, "nfsfs.c: remove_request: invalid request type %d\n", rq->rt);
	}

	for (NFS_BaseRequest* brq = NfsRequests; brq != NULL; brq = brq->next) {
		dprintf(1, "&&& Rem NFS_BaseRequest: %d, %p, %p, %p\n", brq->token, brq, brq->next, brq->previous);
	}
}

static
void 
lookup_cb(uintptr_t token, int status, struct cookie *fh, fattr_t *attr) {
	dprintf(1, "*** nfsfs_lookup_cb: %d, %d, %p, %p\n", token, status, fh, attr);

	// TODO: There is a loop in the BaseRequest loop code
	NFS_LookupRequest *rq = NULL;
	for (NFS_BaseRequest* brq = NfsRequests; brq != NULL; brq = brq->next) {
		dprintf(1, "nfsfs_lookup_cb: NFS_BaseRequest: %d\n", brq->token);
		if (brq->token == token) {
			rq = (NFS_LookupRequest *) brq;
			break;
		}
	}

	dprintf(1, "*** nfsfs_lookup_cb: Finished NFS_BaseRequests\n");

	if (rq == NULL) {
		dprintf(0, "!!!nfsfs: Corrupt lookup callback, no matching token: %d\n", token);
		return;
	}

	// open done
	if (status == NFS_OK) {
		NFS_File *nf = (NFS_File *) rq->vnode->extra;
		memcpy((void *) &(nf->fh), (void *) fh, sizeof(struct cookie));
		memcpy((void *) &(nf->attr), (void *) attr, sizeof(fattr_t));
		dprintf(1, "nfsfs: Sending: %d, %d\n", token, L4_ThreadNo(rq->tid));
		*(rq->rval) = 0;
		rq->open_done(rq->tid, rq->vnode, rq->vnode->path, rq->mode, rq->rval);
		syscall_reply(rq->tid);
		remove_request((NFS_BaseRequest *) rq);
	}

	// create the file
	else if ((status == NFSERR_NOENT) && (rq->mode & FM_WRITE)) {
		dprintf(1, "nfsfs: Create new file!\n");
		// reuse current rq struct, has all we need and is hot and ready
		sattr_t sat = DEFAULT_SATTR;
		nfs_create(&mnt_point, rq->vnode->path, &sat, lookup_cb, rq->token);
	}

	// error
	else {
		dprintf(0, "!!!nfsfs: lookup_cb: Error occured! (%d)\n", status);
		free((NFS_File *) rq->vnode->extra);
		free(rq->vnode);
		*(rq->rval) = (-1);
		syscall_reply(rq->tid);
		remove_request((NFS_BaseRequest *) rq);
	}
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

	if (self == NULL) {
		dprintf(0, "!!! nfsfs_close: Trying to close null file!\n");
		*rval = -1;
		syscall_reply(tid);
	}

	// reduce ref count and free if no longer needed
	self->refcount--;
	if (self->refcount <= 0) {
		free((NFS_File *) self->extra);
		free(self);
		self = NULL;
	}

	*rval = (0);
	syscall_reply(tid);
	close_done(tid, self, file, mode, rval);
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
			break;
		}
	}

	if (rq == NULL) {
		dprintf(0, "!!!nfsfs: Corrupt read callback, no matching token: %d\n", token);
		return;
	}

	if (status != NFS_OK) {
		free((NFS_File *) rq->vnode->extra);
		free(rq->vnode);
		*(rq->rval) = (-1);
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

	if (nbyte >= NFS_BUFSIZ) {
		nbyte = NFS_BUFSIZ - 1;
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
			break;
		}
	}

	if (rq == NULL) {
		dprintf(0, "!!!nfsfs: Corrupt write callback, no matching token: %d\n", token);
		return;
	}

	if (status != NFS_OK) {
		free((NFS_File *) rq->vnode->extra);
		free(rq->vnode);
		*(rq->rval) = (-1);
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
	if (nbyte >= NFS_BUFSIZ) {
		dprintf(0, "!!! nfsfs_write: Write request size too large! (tid %d) (file %d) (size %d) (max %d)!\n",
				L4_ThreadNo(tid), file, nbyte, NFS_BUFSIZ);
		*rval = (-1);
		syscall_reply(tid);
		return;
	}

	NFS_WriteRequest *rq = (NFS_WriteRequest *) create_request(RT_WRITE);
	rq->tid = tid;
	rq->vnode = self;
	rq->file = file;
	rq->buf = (char *) buf;
	rq->rval = rval;
	rq->write_done = write_done;

	// Set rval to nbyte already, will reset if error occurs in write_cb
	*rval = nbyte;

	dprintf(2, "nfsfs: nfs_write call (token %u)\n", rq->token);
	nfs_write(&(nf->fh), offset, nbyte, rq->buf, write_cb, rq->token);
}

static
void
getdirent_cb(uintptr_t token, int status, int num_entries, struct nfs_filename *filenames,
		int next_cookie) {
	dprintf(1, "*** nfsfs_dirent_cb: %d, %d, %d, %d\n", token, status, num_entries, next_cookie);

	NFS_DirRequest *rq = NULL;
	for (NFS_BaseRequest* brq = NfsRequests; brq != NULL; brq = brq->next) {
		dprintf(1, "nfsfs_dirent_cb: NFS_BaseRequest: %d\n", brq->token);
		if (brq->token == token) {
			rq = (NFS_DirRequest *) brq;
			break;
		}
	}

	if (rq == NULL) {
		dprintf(0, "!!!nfsfs: Corrupt dirent callback, no matching token: %d\n", token);
		return;
	}

	if (rq->cpos + num_entries >= rq->pos + 1) {
		// got it
		dprintf(2, "found file, getting now\n");
		struct nfs_filename *nfile = &filenames[rq->pos - rq->cpos];
		if (nfile->size + 1 <= rq->nbyte) {
			strncpy(rq->buf, nfile->file, nfile->size);
			rq->buf[nfile->size] = '\0';
			dprintf(2, "File: %s\n", rq->buf);
			*(rq->rval) = nfile->size;
			syscall_reply(rq->tid);
			remove_request((NFS_BaseRequest *) rq);
			return;
		}
	} else if (next_cookie > 0) {
		// need later directory entry
		dprintf(2, "Need more dir entries to get file\n");
		rq->cpos += num_entries;
		nfs_readdir(&mnt_point, next_cookie, NFS_BUFSIZ, getdirent_cb, rq->token);
		return;
	}

	// error case, just return 0 to say nothing read, its not an error just eof
	dprintf(2, "nfsfs_getdirent: didnt find file (%d)\n", rq->pos);
	*(rq->rval) = 0;
	syscall_reply(rq->tid);
	remove_request((NFS_BaseRequest *) rq);
}

void
nfsfs_getdirent(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte,
		int *rval) {
	dprintf(1, "*** nfsfs_getdirent: %p, %d, %p, %d\n", self, pos, name, nbyte);

	NFS_DirRequest *rq = (NFS_DirRequest *) create_request(RT_DIR);

	rq->tid = tid;
	rq->vnode = self;
	rq->pos = pos;
	rq->buf = name;
	rq->nbyte = nbyte;
	rq->rval = rval;
	rq->cpos = 0;

	nfs_readdir(&mnt_point, 0, NFS_BUFSIZ, getdirent_cb, rq->token);
}

static
void 
stat_cb(uintptr_t token, int status, struct cookie *fh, fattr_t *attr) {
	dprintf(1, "*** nfsfs_stat_cb: %d, %d, %p, %p\n", token, status, fh, attr);

	NFS_StatRequest *rq = NULL;
	for (NFS_BaseRequest* brq = NfsRequests; brq != NULL; brq = brq->next) {
		dprintf(1, "nfsfs_stat_cb: NFS_BaseRequest: %d\n", brq->token);
		if (brq->token == token) {
			rq = (NFS_StatRequest *) brq;
			break;
		}
	}

	if (rq == NULL) {
		dprintf(0, "!!!nfsfs: Corrupt stat callback, no matching token: %d\n", token);
		return;
	}

	// fail
	if (status != NFS_OK) {
		*(rq->rval) = (-1);
		syscall_reply(rq->tid);
		remove_request((NFS_BaseRequest *) rq);
		return;
	}

	// good
	rq->stat->st_type = attr->type;
	rq->stat->st_fmode = attr->mode;
	rq->stat->st_size = attr->size;
	rq->stat->st_ctime = (attr->ctime.seconds * 1000) + (attr->ctime.useconds);
	rq->stat->st_atime = (attr->atime.seconds * 1000) + (attr->atime.useconds);
	*(rq->rval) = (0);
	syscall_reply(rq->tid);
	remove_request((NFS_BaseRequest *) rq);
}

void
nfsfs_stat(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf, int *rval) {
	dprintf(1, "*** nfsfs_stat: %p, %s, %p\n", self, path, buf);

	if (self != NULL) {
		NFS_File *nf = (NFS_File *) self->extra;
		if (nf == NULL) {
			dprintf(0, "!!! nfsfs_stat: Broken NFS file! No nfs struct! (file %s)\n", path);
			*rval = (-1);
			syscall_reply(tid);
			return;
		}
		buf->st_type = nf->attr.type;
		buf->st_fmode = nf->attr.mode;
		buf->st_size = nf->attr.size;
		buf->st_ctime = (nf->attr.ctime.seconds * 1000) + (nf->attr.ctime.useconds);
		buf->st_atime = (nf->attr.atime.seconds * 1000) + (nf->attr.atime.useconds);
		*rval = (0);
		syscall_reply(tid);
		return;
	}

	// stat non open file
	dprintf(1, "*** nfsfs_stat: trying to stat non open file! (file %s)\n", path);

	NFS_StatRequest *rq = (NFS_StatRequest *) create_request(RT_STAT);

	rq->vnode = self;
	rq->tid = tid;
	rq->stat = buf;
	rq->rval = rval;

	char *path2 = (char *) path;
	nfs_lookup(&mnt_point, path2, stat_cb, rq->token);
}

