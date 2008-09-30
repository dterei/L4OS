#include <string.h>
#include <nfs/nfs.h>

#include "nfsfs.h"

#include "libsos.h"
#include "network.h"
#include "syscall.h"
#include "constants.h"

#define verbose 0

/******** NFS TIMEOUT THREAD ********/
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
		dprintf(4, "*** nfs_timeout_thread: timout event!\n");
	}
}


/******** NFS FS ********/
#define NULL_TOKEN ((uintptr_t) (-1))

/*
 * NFS Permissions.
 * _ | ___ | ___ | ___
 * s u rwx g rwx o rwx
 * 512     ...       1
 * Set default to rw for all.
 */
#define DEFAULT_SATTR { (438), (0), (0), (0), {(0), (0)}, {(0), (0)} }

/* NFS File */
typedef struct {
	VNode vnode;
	struct cookie fh;
} NFS_File;

/* Types of NFS request, used for continuations until callbacks */
enum NfsRequestType {
	RT_LOOKUP, /* aka OPEN */
	RT_READ,
	RT_WRITE,
	RT_STAT,
	RT_DIR,
	RT_REMOVE
};

typedef struct NFS_BaseRequest_t NFS_BaseRequest;

/* Base NFS request object.
 * Make sure all other request have this struct as their first element
 * so we can treat them as this base class, c 'OO' style.
 */
struct NFS_BaseRequest_t {
	enum NfsRequestType rt;
	uintptr_t token;
	VNode vnode;
	L4_ThreadId_t tid;
	int *rval;

	NFS_BaseRequest *previous;
	NFS_BaseRequest *next;
};

typedef struct {
	NFS_BaseRequest p;
	fmode_t mode;
	void (*open_done) (L4_ThreadId_t tid, VNode self, fmode_t mode, int status);
} NFS_LookupRequest;

/* Could combine read and write since are the same, but prefer separate for
 * easy extension
 */
typedef struct {
	NFS_BaseRequest p;
	fildes_t file;
	char *buf;
	void (*read_done)(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos, char *buf,
			size_t nbyte, int *rval);
} NFS_ReadRequest;

typedef struct {
	NFS_BaseRequest p;
	fildes_t file;
	char *buf;
	void (*write_done)(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
			const char *buf, size_t nbyte, int *rval);
} NFS_WriteRequest;

typedef struct {
	NFS_BaseRequest p;
	stat_t *stat;
} NFS_StatRequest;

typedef struct {
	NFS_BaseRequest p;
	int pos;
	char *buf;
	size_t nbyte;
	int cpos;
} NFS_DirRequest;

typedef struct {
	NFS_BaseRequest p;
	const char *path;
} NFS_RemoveRequest;

/* Queue of request for callbacks */
NFS_BaseRequest *NfsRequests;

/* NFS Base directory */
struct cookie nfs_mnt;


/* Start up NFS file system */
int
nfsfs_init(void) {
	dprintf(1, "*** nfsfs_init\n");
	
	/* redefine just to limit our linkage to one place */
	nfs_mnt = mnt_point;

	NfsRequests = NULL;
	(void) sos_thread_new(L4_nilthread,
			&nfsfs_timeout_thread, &nfsfs_timer_stack[STACK_SIZE]);
	return 0;
}

/* Create a new unique token for the NFS callbacks */
static
uintptr_t
newtoken(void) {
	static uintptr_t tok = 0;
	tok++;
	return (tok % ( NULL_TOKEN - 1));
}

/* Create a new NFS request of type specified */
static
NFS_BaseRequest *
create_request(enum NfsRequestType rt, VNode vn, L4_ThreadId_t tid, int *rval) {
	//XXX: Check for malloc fail!
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
		case RT_REMOVE:
			rq = (NFS_BaseRequest *) malloc(sizeof(NFS_RemoveRequest));
			rq->rt = RT_REMOVE;
			break;
		default:
			dprintf(0, "!!! nfsfs_create_request: invalid request type %d\n", rt);
			return NULL;
	}

	rq->token = newtoken();
	rq->vnode = vn;
	rq->tid = tid;
	rq->rval = rval;

	rq->next = NULL;
	rq->previous = NULL;

	// add to list if list not empty
	if (NfsRequests != NULL) {
		rq->next = NfsRequests;
		NfsRequests->previous = rq;
	}
	NfsRequests = rq;

	return NfsRequests;
}

/* Remove and free a specified request */
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
		case RT_REMOVE:
			free((NFS_RemoveRequest *) rq);
			break;
		default:
			dprintf(0, "nfsfs.c: remove_request: invalid request type %d\n", rq->rt);
	}
}

/* Return a NFS request struct with the token specified */
static
NFS_BaseRequest*
get_request(uintptr_t token) {
	for (NFS_BaseRequest* brq = NfsRequests; brq != NULL; brq = brq->next) {
		dprintf(2, "nfsfs_read_cb: NFS_BaseRequest: %d\n", brq->token);
		if (brq->token == token) {
			return brq;
		}
	}

	return NULL;
}

/* Convert the NFS mode (xwr) to unix mode (rwx) */
static fmode_t
mode_nfs2unix(fmode_t mode) {
	return
		(((mode & 0x1) >> 0) << 2) |
		(((mode & 0x4) >> 2) << 1) |
		(((mode & 0x2) >> 1) << 0);
}

/* Copy the relavent entriees from attr to buf */
static
void
cp_stats(stat_t *stat, fattr_t *attr) {
	stat->st_type  = attr->type;
	stat->st_fmode = mode_nfs2unix(attr->mode);
	stat->st_size  = attr->size;
	stat->st_ctime = (attr->ctime.seconds * 1000) + (attr->ctime.useconds);
	stat->st_atime = (attr->atime.seconds * 1000) + (attr->atime.useconds);
}

/* NFS_LookUp Callback */
static
void 
lookup_cb(uintptr_t token, int status, struct cookie *fh, fattr_t *attr) {
	dprintf(1, "*** nfsfs_lookup_cb: %d, %d, %p, %p\n", token, status, fh, attr);

	NFS_LookupRequest *rq = (NFS_LookupRequest *) get_request(token);
	if (rq == NULL) {
		dprintf(0, "!!!nfsfs: Corrupt lookup callback, no matching token: %d\n", token);
		return;
	}

	// open done
	if (status == NFS_OK) {
		NFS_File *nf = (NFS_File *) rq->p.vnode->extra;
		memcpy((void *) &(nf->fh), (void *) fh, sizeof(struct cookie));
		cp_stats(&(rq->p.vnode->vstat), attr);
		dprintf(1, "nfsfs: Sending: %d, %d\n", token, L4_ThreadNo(rq->p.tid));
		rq->open_done(rq->p.tid, rq->p.vnode, rq->mode, SOS_VFS_OK);
		remove_request((NFS_BaseRequest *) rq);
	}

	// create the file
	else if ((status == NFSERR_NOENT) && (rq->mode & FM_WRITE)) {
		dprintf(1, "nfsfs: Create new file!\n");
		// reuse current rq struct, has all we need and is hot and ready
		sattr_t sat = DEFAULT_SATTR;
		nfs_create(&nfs_mnt, rq->p.vnode->path, &sat, lookup_cb, rq->p.token);
	}

	// error
	else {
		dprintf(0, "!!!nfsfs: lookup_cb: Error occured! (%d)\n", status);
		free((NFS_File *) rq->p.vnode->extra);
		free(rq->p.vnode);
		if (status == NFSERR_NOENT) {
			rq->open_done(rq->p.tid, rq->p.vnode, rq->mode, SOS_VFS_NOVNODE);
		} else {
			rq->open_done(rq->p.tid, rq->p.vnode, rq->mode, SOS_VFS_ERROR);
		}
		remove_request((NFS_BaseRequest *) rq);
	}
}

/* Handle opening an NFS file */
static
void
getvnode(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		void (*open_done)(L4_ThreadId_t tid, VNode self, fmode_t mode, int status)) {
	dprintf(1, "*** nfsfs_getvnode: %p, %s, %d, %p\n", self, path, mode, open_done);

	self = (VNode) malloc(sizeof(struct VNode_t));
	if (self == NULL) {
		dprintf(0, "!!! nfsfs_getvnode: Malloc Failed! cant create new vnode !!!\n");
		open_done(tid, self, mode, SOS_VFS_NOMEM);
		return;
	}

	strncpy(self->path, path, N_NAME);
	self->refcount = 0;
	self->vstat.st_type = ST_FILE;
	self->next = NULL;
	self->previous = NULL;

	NFS_File *nf = (NFS_File *) malloc(sizeof(NFS_File));
	nf->vnode = self;
	self->extra = (void *) nf;

	self->open = nfsfs_open;
	self->close = nfsfs_close;
	self->read = nfsfs_read;
	self->write = nfsfs_write;
	self->getdirent = nfsfs_getdirent;
	self->stat = nfsfs_stat;
	self->remove = nfsfs_remove;

	NFS_LookupRequest *rq = (NFS_LookupRequest *)
		create_request(RT_LOOKUP, self, tid, NULL);

	rq->mode = mode;
	rq->open_done = open_done;

	nfs_lookup(&nfs_mnt, (char *) path, lookup_cb, rq->p.token);
}

/* Open a specified file using NFS */
void
nfsfs_open(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		void (*open_done)(L4_ThreadId_t tid, VNode self, fmode_t mode, int status)) {
	dprintf(1, "*** nfsfs_open: %p, %s, %d, %p\n", self, path, mode, open_done);

	if (self == NULL) {
		dprintf(2, "nfs_open: get new vnode\n");
		getvnode(tid, self, path, mode, open_done);
	} else {
		dprintf(2, "nfs_open: already open vnode, increase refcount\n\n");
		open_done(tid, self, mode, SOS_VFS_OK);
	}
};

/* Close a specified file previously opened with nfsfs_open, don't free the vnode
 * just free nfs specific file structs as vfs will free the vnode
 */
void
nfsfs_close(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
		int *rval, void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
			int *rval)) {
	dprintf(1, "*** nfsfs_close: %p, %d, %d, %p\n", self, file, mode, close_done);

	if (self == NULL) {
		dprintf(0, "!!! nfsfs_close: Trying to close null file!\n");
		*rval = SOS_VFS_NOFILE;
		syscall_reply(tid, *rval);
		close_done(tid, self, file, mode, rval);
		return;
	}

	// reduce ref count and free if no longer needed
	self->refcount--;
	dprintf(2, "refcount: %d\n", self->refcount);
	if (self->refcount <= 0) {
		free((NFS_File *) self->extra);
		self = NULL;
	}

	*rval = SOS_VFS_OK;
	syscall_reply(tid, *rval);
	close_done(tid, self, file, mode, rval);
}

/* NFS callback for nfs_read */
static
void 
read_cb(uintptr_t token, int status, fattr_t *attr, int bytes_read, char *data) {
	dprintf(1, "*** nfsfs_read_cb: %u, %d, %d, %p\n", token, status, bytes_read, data);

	NFS_ReadRequest *rq = (NFS_ReadRequest *) get_request(token);
	if (rq == NULL) {
		dprintf(0, "!!!nfsfs: Corrupt read callback, no matching token: %d\n", token);
		return;
	}

	if (status != NFS_OK) {
		*(rq->p.rval) = SOS_VFS_ERROR;
		rq->read_done(rq->p.tid, rq->p.vnode, rq->file, 0, rq->buf, 0, rq->p.rval);
		syscall_reply(rq->p.tid, *(rq->p.rval));
		remove_request((NFS_BaseRequest *) rq);
		return;
	}

	memcpy((void *) rq->buf, (void *) data, bytes_read);
	cp_stats(&(rq->p.vnode->vstat), attr);
	*(rq->p.rval) = bytes_read;

	// call vfs to handle fp and anything else
	rq->read_done(rq->p.tid, rq->p.vnode, rq->file, 0, rq->buf, bytes_read, rq->p.rval);
	syscall_reply(rq->p.tid, *(rq->p.rval));
	remove_request((NFS_BaseRequest *) rq);
}

/* Read a specified number of bytes into a buffer from the given NFS file */
void
nfsfs_read(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, int *rval, void (*read_done)(L4_ThreadId_t tid,
			VNode self, fildes_t file, L4_Word_t pos, char *buf, size_t nbyte, int *rval)) {
	dprintf(1, "*** nfsfs_read: %p, %d, %d, %p, %d\n", self, file, pos, buf, nbyte);

	NFS_File *nf = (NFS_File *) self->extra;	
	if (nf == NULL) {
		dprintf(0, "!!! nfsfs_read: Invalid NFS file (p %d, f %d), no nfs struct!\n",
				L4_ThreadNo(tid), file);
		*rval = SOS_VFS_NOFILE;
		syscall_reply(tid, *rval);
		return;
	}

	if (nbyte >= NFS_BUFSIZ) {
		dprintf(1, "tried to read too much data at once: %d\n", nbyte);
		nbyte = NFS_BUFSIZ - 1;
	}

	NFS_ReadRequest *rq = (NFS_ReadRequest *) create_request(RT_READ, self, tid, rval);
	rq->file = file;
	rq->buf = buf;
	rq->read_done = read_done;

	nfs_read(&(nf->fh), pos, nbyte, read_cb, rq->p.token);
}

/* NFS Callback for NFS_Write */
static
void
write_cb(uintptr_t token, int status, fattr_t *attr) {
	dprintf(1, "*** nfsfs_write_cb: %u, %d, %p\n", token, status, attr);

	NFS_WriteRequest *rq = (NFS_WriteRequest *) get_request(token);
	if (rq == NULL) {
		dprintf(0, "!!!nfsfs: Corrupt write callback, no matching token: %d\n", token);
		return;
	}

	if (status != NFS_OK) {
		*(rq->p.rval) = SOS_VFS_ERROR;
		rq->write_done(rq->p.tid, rq->p.vnode, rq->file, 0, rq->buf, 0, rq->p.rval);
		syscall_reply(rq->p.tid, *(rq->p.rval));
		remove_request((NFS_BaseRequest *) rq);
		return;
	}

	cp_stats(&(rq->p.vnode->vstat), attr);

	// call vfs to handle fp and anything else
	rq->write_done(rq->p.tid, rq->p.vnode, rq->file, 0, rq->buf, (size_t) *(rq->p.rval), rq->p.rval);
	syscall_reply(rq->p.tid, *(rq->p.rval));
	remove_request((NFS_BaseRequest *) rq);
}

/* Write the specified number of bytes from the buffer buf to a given NFS file */
void
nfsfs_write(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int *rval, void (*write_done)(L4_ThreadId_t tid,
			VNode self, fildes_t file, L4_Word_t offset, const char *buf, size_t nbyte,
			int *rval)) {
	dprintf(1, "*** nfsfs_write: %p, %d, %d, %p, %d\n", self, file, offset, buf, nbyte);

	NFS_File *nf = (NFS_File *) self->extra;	
	if (nf == NULL) {
		dprintf(0, "!!! nfsfs_write: Invalid NFS file (p %d, f %d), no nfs struct!\n",
				L4_ThreadNo(tid), file);
		*rval = SOS_VFS_NOFILE;
		syscall_reply(tid, *rval);
		return;
	}

	if (nbyte >= NFS_BUFSIZ) {
		dprintf(1, "!!! nfsfs_write: request too large! (tid %d) (file %d) (size %d) (max %d)!\n",
				L4_ThreadNo(tid), file, nbyte, NFS_BUFSIZ);
		nbyte = NFS_BUFSIZ - 1;
	}

	NFS_WriteRequest *rq = (NFS_WriteRequest *) create_request(RT_WRITE, self, tid, rval);
	rq->file = file;
	rq->buf = (char *) buf;
	rq->write_done = write_done;

	// Set rval to nbyte already, will reset if error occurs in write_cb
	*rval = nbyte;

	nfs_write(&(nf->fh), offset, nbyte, rq->buf, write_cb, rq->p.token);
}

/* NFS Callback for NFS_getdirent */
static
void
getdirent_cb(uintptr_t token, int status, int num_entries, struct nfs_filename *filenames,
		int next_cookie) {
	dprintf(1, "*** nfsfs_dirent_cb: %d, %d, %d, %d\n", token, status, num_entries, next_cookie);

	NFS_DirRequest *rq = (NFS_DirRequest *) get_request(token);
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
			*(rq->p.rval) = nfile->size;
			syscall_reply(rq->p.tid, *(rq->p.rval));
			remove_request((NFS_BaseRequest *) rq);
			return;
		}
	} else if (next_cookie > 0) {
		// need later directory entry
		dprintf(2, "Need more dir entries to get file\n");
		rq->cpos += num_entries;
		nfs_readdir(&nfs_mnt, next_cookie, NFS_BUFSIZ, getdirent_cb, rq->p.token);
		return;
	}

	// error case, just return SOS_VFS_OK to say nothing read, its not an error just eof
	dprintf(2, "nfsfs_getdirent: didnt find file (%d)\n", rq->pos);
	*(rq->p.rval) = SOS_VFS_EOF;
	syscall_reply(rq->p.tid, *(rq->p.rval));
	remove_request((NFS_BaseRequest *) rq);
}

/* Get directory entries of the NFS filesystem */
void
nfsfs_getdirent(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte,
		int *rval) {
	dprintf(1, "*** nfsfs_getdirent: %p, %d, %p, %d\n", self, pos, name, nbyte);

	NFS_DirRequest *rq = (NFS_DirRequest *) create_request(RT_DIR, self, tid, rval);

	rq->pos = pos;
	rq->buf = name;
	rq->nbyte = nbyte;
	rq->cpos = 0;

	nfs_readdir(&nfs_mnt, 0, NFS_BUFSIZ, getdirent_cb, rq->p.token);
}

/* NFS Callback for NFS_Stat */
static
void 
stat_cb(uintptr_t token, int status, struct cookie *fh, fattr_t *attr) {
	dprintf(1, "*** nfsfs_stat_cb: %d, %d, %p, %p\n", token, status, fh, attr);

	NFS_StatRequest *rq = (NFS_StatRequest *) get_request(token);
	if (rq == NULL) {
		dprintf(0, "!!!nfsfs: Corrupt stat callback, no matching token: %d\n", token);
		return;
	}

	// fail
	if (status != NFS_OK) {
		*(rq->p.rval) = SOS_VFS_ERROR;
		syscall_reply(rq->p.tid, *(rq->p.rval));
		remove_request((NFS_BaseRequest *) rq);
	}
	// good
	else {
		cp_stats(rq->stat, attr);
		*(rq->p.rval) = SOS_VFS_OK;
		dprintf(1, "nfsfs_stat_cb: Copied fine, reply to %d, value %d\n", L4_ThreadNo(rq->p.tid), *(rq->p.rval));
		syscall_reply(rq->p.tid, *(rq->p.rval));
		remove_request((NFS_BaseRequest *) rq);
	}
}

/* Get file details for a specified NFS File */
void
nfsfs_stat(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf, int *rval) {
	dprintf(1, "*** nfsfs_stat: %p, %s, %p\n", self, path, buf);

	if (self != NULL) {
		NFS_File *nf = (NFS_File *) self->extra;
		if (nf == NULL) {
			dprintf(0, "!!! nfsfs_stat: Broken NFS file! No nfs struct! (file %s)\n",
					path);
			*rval = SOS_VFS_ERROR;
			syscall_reply(tid, *rval);
			return;
		}

		memcpy((void *) buf, (void *) &(self->vstat), sizeof(stat_t));
		*rval = SOS_VFS_OK;
		syscall_reply(tid, *rval);
	}
	
	// stat non open file
	else {
		dprintf(1, "*** nfsfs_stat: trying to stat non open file! (file %s)\n", path);

		NFS_StatRequest *rq = (NFS_StatRequest *)
			create_request(RT_STAT, self, tid, rval);
		rq->stat = buf;

		nfs_lookup(&nfs_mnt, (char *) path, stat_cb, rq->p.token);
	}
}

/* NFS Callback for NFS_Remove */
static
void
remove_cb(uintptr_t token, int status) {
	dprintf(1, "*** nfsfs: remove_cb %u %d *** \n", token, status);

	NFS_RemoveRequest *rq = (NFS_RemoveRequest *) get_request(token);
	if (rq == NULL) {
		dprintf(0, "!!!nfsfs: Corrupt remove callback, no matching token: %d\n", token);
		return;
	}

	if (status == NFS_OK) {
		*(rq->p.rval) = SOS_VFS_OK;
	} else {
		*(rq->p.rval) = SOS_VFS_ERROR;
	}

	syscall_reply(rq->p.tid, *(rq->p.rval));
	remove_request((NFS_BaseRequest *) rq);
}

/* Remove a file */
void
nfsfs_remove(L4_ThreadId_t tid, VNode self, const char *path, int *rval) {
	dprintf(1, "*** nfsfs_remove: %d %s ***\n", L4_ThreadNo(tid), path);

	if (self != NULL) {
		// cant remove open files
		*rval = SOS_VFS_ERROR;
		syscall_reply(tid, *rval);
	} else {
		// remove file
		NFS_RemoveRequest *rq = (NFS_RemoveRequest *)
			create_request(RT_REMOVE, self, tid, rval);
		rq->path = path;
		nfs_remove(&nfs_mnt, (char *) path, remove_cb, rq->p.token);
	}
}

