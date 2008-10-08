#include <string.h>
#include <nfs/nfs.h>

#include "nfsfs.h"

#include "libsos.h"
#include "network.h"
#include "syscall.h"
#include "constants.h"

#define verbose 1

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
	L4_KDB_SetThreadName(sos_my_tid(), "nfsfs_timeout");
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

#define DEFAULT_MAX_READERS VFS_UNLIMITED_RW
#define DEFAULT_MAX_WRITERS VFS_UNLIMITED_RW

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
	RT_REMOVE,
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
			size_t nbyte, int status);
} NFS_ReadRequest;

typedef struct {
	NFS_BaseRequest p;
	fildes_t file;
	char *buf;
	size_t nbyte;
	void (*write_done)(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
			const char *buf, size_t nbyte, int status);
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
	return ((tok++) % (NULL_TOKEN - 1));
}

/* Create a new NFS request of type specified */
static
NFS_BaseRequest *
create_request(enum NfsRequestType rt, VNode vn, L4_ThreadId_t tid) {
	dprintf(2, "create request: %d %p %d\n", rt, vn, L4_ThreadNo(tid));
	NFS_BaseRequest *rq;

	switch (rt) {
		case RT_LOOKUP:
			rq = (NFS_BaseRequest *) malloc(sizeof(NFS_LookupRequest));
			break;
		case RT_READ:
			rq = (NFS_BaseRequest *) malloc(sizeof(NFS_ReadRequest));
			break;
		case RT_WRITE:
			rq = (NFS_BaseRequest *) malloc(sizeof(NFS_WriteRequest));
			break;
		case RT_STAT:
			rq = (NFS_BaseRequest *) malloc(sizeof(NFS_StatRequest));
			break;
		case RT_DIR:
			rq = (NFS_BaseRequest *) malloc(sizeof(NFS_DirRequest));
			break;
		case RT_REMOVE:
			rq = (NFS_BaseRequest *) malloc(sizeof(NFS_RemoveRequest));
			break;
		default:
			dprintf(0, "!!! nfsfs_create_request: invalid request type %d\n", rt);
			return NULL;
	}

	if (rq == NULL) {
		dprintf(0, "!!! nfsfs_create_request: Malloc failed! (type %d)\n", rt);
		return NULL;
	}

	rq->rt = rt;
	rq->token = newtoken();
	rq->vnode = vn;
	rq->tid = tid;

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
			dprintf(0, "!!! nfsfs.c: remove_request: invalid request type %d\n", rq->rt);
	}
}

/* Return a NFS request struct with the token specified */
static
NFS_BaseRequest*
get_request(uintptr_t token) {
	for (NFS_BaseRequest* brq = NfsRequests; brq != NULL; brq = brq->next) {
		dprintf(3, "nfsfs_read_cb: NFS_BaseRequest: %d\n", brq->token);
		if (brq->token == token) {
			return brq;
		}
	}

	return NULL;
}

/* Convert the NFS mode (xwr) to unix mode (rwx) */
static
fmode_t
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

/* Change an NFS Error into a VFS Error */
static
L4_Word_t
status_nfs2vfs(int status) {
	switch(status) {
		case NFS_OK:					return SOS_VFS_OK;		break;
		case NFSERR_PERM:				return SOS_VFS_PERM;		break;
		case NFSERR_NOENT:			return SOS_VFS_NOVNODE;	break;
		case NFSERR_NAMETOOLONG:	return SOS_VFS_NOMEM;	break;
		case NFSERR_IO:
		case NFSERR_NXIO:
		case NFSERR_ACCES:
		case NFSERR_EXIST:
		case NFSERR_NODEV:
		case NFSERR_NOTDIR:
		case NFSERR_ISDIR:
		case NFSERR_FBIG:
		case NFSERR_NOSPC:
		case NFSERR_ROFS:
		case NFSERR_NOTEMPTY:
		case NFSERR_DQUOT:
		case NFSERR_STALE:
		case NFSERR_WFLUSH:
		default:							return SOS_VFS_ERROR; 	break;
	}
}

/* NFS_LookUp Callback */
static
void 
lookup_cb(uintptr_t token, int status, struct cookie *fh, fattr_t *attr) {
	dprintf(1, "*** nfsfs_lookup_cb: %d, %d, %p, %p\n", token, status, fh, attr);

	NFS_LookupRequest *rq = (NFS_LookupRequest *) get_request(token);
	if (rq == NULL) {
		dprintf(0, "!!! nfsfs: Corrupt lookup callback, no matching token: %d\n", token);
		return;
	}

	// open done
	if (status == NFS_OK) {
		NFS_File *nf = (NFS_File *) rq->p.vnode->extra;
		memcpy(&(nf->fh), fh, sizeof(struct cookie));
		cp_stats(&(rq->p.vnode->vstat), attr);
		dprintf(2, "nfsfs: Sending: %d, %d\n", token, L4_ThreadNo(rq->p.tid));
		rq->open_done(rq->p.tid, rq->p.vnode, rq->mode, SOS_VFS_OK);
		remove_request((NFS_BaseRequest *) rq);
	}

	// create the file
	else if ((status == NFSERR_NOENT) && (rq->mode & FM_WRITE)) {
		dprintf(2, "nfsfs: Create new file!\n");
		// reuse current rq struct, has all we need and is hot and ready
		sattr_t sat = DEFAULT_SATTR;
		nfs_create(&nfs_mnt, rq->p.vnode->path, &sat, lookup_cb, rq->p.token);
	}

	// error
	else {
		dprintf(0, "!!! nfsfs: lookup_cb: Error occured! (%d)\n", status);
		free((NFS_File *) rq->p.vnode->extra);
		free(rq->p.vnode);
		rq->open_done(rq->p.tid, NULL, rq->mode, status_nfs2vfs(status));
		remove_request((NFS_BaseRequest *) rq);
	}
}

/* Open a specified file using NFS */
void
nfsfs_open(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		void (*open_done)(L4_ThreadId_t tid, VNode self, fmode_t mode, int status)) {
	dprintf(1, "*** nfsfs_open: %p, %s, %d, %p\n", self, path, mode, open_done);

	memcpy( (void *) self->path, (void *) path, MAX_FILE_NAME);
	self->readers = 0;
	self->writers = 0;
	self->Max_Readers = DEFAULT_MAX_READERS;
	self->Max_Writers = DEFAULT_MAX_WRITERS;
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

	NFS_LookupRequest *rq = (NFS_LookupRequest *) create_request(RT_LOOKUP, self, tid);
	rq->mode = mode;
	rq->open_done = open_done;

	// If open mode is write, then create new file since we want to start again.
	if (mode & FM_WRITE) {
		sattr_t sat = DEFAULT_SATTR;
		nfs_create(&nfs_mnt, rq->p.vnode->path, &sat, lookup_cb, rq->p.token);
	} else {
		nfs_lookup(&nfs_mnt, (char *) path, lookup_cb, rq->p.token);
	}
}

/* Close a specified file previously opened with nfsfs_open, don't free the vnode
 * just free nfs specific file structs as vfs will free the vnode
 */
void
nfsfs_close(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
		void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode, int status)) {
	dprintf(1, "*** nfsfs_close: %p, %d, %d, %p\n", self, file, mode, close_done);

	if (self == NULL) {
		dprintf(0, "!!! nfsfs_close: Trying to close null file!\n");
		close_done(tid, self, file, mode, SOS_VFS_NOFILE);
		return;
	}

	free((NFS_File *) self->extra);
	self->extra = NULL;

	close_done(tid, self, file, mode, SOS_VFS_OK);
}

/* NFS callback for nfs_read */
static
void 
read_cb(uintptr_t token, int status, fattr_t *attr, int bytes_read, char *data) {
	dprintf(1, "*** nfsfs_read_cb: %u, %d, %d, %p\n", token, status, bytes_read, data);

	NFS_ReadRequest *rq = (NFS_ReadRequest *) get_request(token);
	if (rq == NULL) {
		dprintf(0, "!!! nfsfs: Corrupt read callback, no matching token: %d\n", token);
		return;
	}

	if (status != NFS_OK) {
		rq->read_done(rq->p.tid, rq->p.vnode, rq->file, 0, rq->buf, 0, status_nfs2vfs(status));
		remove_request((NFS_BaseRequest *) rq);
		return;
	}

	memcpy((void *) rq->buf, (void *) data, bytes_read);
	cp_stats(&(rq->p.vnode->vstat), attr);

	// call vfs to handle fp and anything else
	rq->read_done(rq->p.tid, rq->p.vnode, rq->file, 0, rq->buf, bytes_read, bytes_read);
	remove_request((NFS_BaseRequest *) rq);
}

/* Read a specified number of bytes into a buffer from the given NFS file */
void
nfsfs_read(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, void (*read_done)(L4_ThreadId_t tid,
			VNode self, fildes_t file, L4_Word_t pos, char *buf, size_t nbyte, int status)) {
	dprintf(1, "*** nfsfs_read: %p, %d, %d, %p, %d\n", self, file, pos, buf, nbyte);

	NFS_File *nf = (NFS_File *) self->extra;	
	if (self == NULL || nf == NULL) {
		dprintf(0, "!!! nfsfs_read: Invalid NFS file (p %d, f %d), no nfs struct!\n",
				L4_ThreadNo(tid), file);
		read_done(tid, self, file, pos, buf, 0, SOS_VFS_NOFILE);
		return;
	}

	if (nbyte > NFS_BUFSIZ) {
		dprintf(2, "nfsfs_read: tried to read too much data at once: %d\n", nbyte);
		nbyte = NFS_BUFSIZ;
	}

	NFS_ReadRequest *rq = (NFS_ReadRequest *) create_request(RT_READ, self, tid);
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
		dprintf(0, "!!! nfsfs: Corrupt write callback, no matching token: %d\n", token);
		return;
	}

	if (status != NFS_OK) {
		rq->write_done(rq->p.tid, rq->p.vnode, rq->file, 0, rq->buf, 0, status_nfs2vfs(status));
		remove_request((NFS_BaseRequest *) rq);
		return;
	}

	cp_stats(&(rq->p.vnode->vstat), attr);

	// call vfs to handle fp and anything else
	rq->write_done(rq->p.tid, rq->p.vnode, rq->file, 0, rq->buf, rq->nbyte, rq->nbyte);
	remove_request((NFS_BaseRequest *) rq);
}

/* Write the specified number of bytes from the buffer buf to a given NFS file */
void
nfsfs_write(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, void (*write_done)(L4_ThreadId_t tid, VNode self,
			fildes_t file, L4_Word_t offset, const char *buf, size_t nbyte, int status)) {
	dprintf(1, "*** nfsfs_write: %p, %d, %d, %p, %d\n", self, file, offset, buf, nbyte);

	NFS_File *nf = (NFS_File *) self->extra;	
	if (nf == NULL) {
		dprintf(0, "!!! nfsfs_write: Invalid NFS file (p %d, f %d), no nfs struct!\n",
				L4_ThreadNo(tid), file);
		write_done(tid, self, file, offset, buf, 0, SOS_VFS_NOFILE);
		return;
	}

	if (nbyte > NFS_BUFSIZ) {
		dprintf(2, "!!! nfsfs_write: request too large! (tid %d) (file %d) (size %d) (max %d)!\n",
				L4_ThreadNo(tid), file, nbyte, NFS_BUFSIZ);
		nbyte = NFS_BUFSIZ;
	}

	NFS_WriteRequest *rq = (NFS_WriteRequest *) create_request(RT_WRITE, self, tid);
	rq->file = file;
	rq->buf = (char *) buf;
	rq->nbyte = nbyte;
	rq->write_done = write_done;

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
		dprintf(0, "!!! nfsfs: Corrupt dirent callback, no matching token: %d\n", token);
		return;
	}

	if (status != NFS_OK) {
		syscall_reply(rq->p.tid, status_nfs2vfs(status));
		remove_request((NFS_BaseRequest *) rq);
		return;
	}


	// got it
	if (rq->cpos + num_entries >= rq->pos + 1) {
		dprintf(2, "found file, getting now\n");
		int status = SOS_VFS_ERROR;
		struct nfs_filename *nfile = &filenames[rq->pos - rq->cpos];
		if (nfile->size + 1 <= rq->nbyte) {
			memcpy(rq->buf, nfile->file, nfile->size);
			rq->buf[nfile->size] = '\0';
			status = nfile->size;
		} else {
			dprintf(0, "!!! nfs_getdirent_cb: Filename too big for given buffer! (%d) (%d)\n",
					nfile->size, rq->nbyte);
			status = SOS_VFS_NOMEM;
		}
		syscall_reply(rq->p.tid, status);
		remove_request((NFS_BaseRequest *) rq);
	}
	// need later directory entry
	else if (next_cookie > 0) {
		dprintf(2, "Need more dir entries to get file\n");
		rq->cpos += num_entries;
		nfs_readdir(&nfs_mnt, next_cookie, NFS_BUFSIZ, getdirent_cb, rq->p.token);
	}
	// error case, just return SOS_VFS_OK to say nothing read, its not an error just eof
	else {
		dprintf(2, "nfsfs_getdirent: didnt find file (%d)\n", rq->pos);
		syscall_reply(rq->p.tid, SOS_VFS_EOF);
		remove_request((NFS_BaseRequest *) rq);
	}
}

/* Get directory entries of the NFS filesystem */
void
nfsfs_getdirent(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte) {
	dprintf(1, "*** nfsfs_getdirent: %p, %d, %p, %d\n", self, pos, name, nbyte);

	NFS_DirRequest *rq = (NFS_DirRequest *) create_request(RT_DIR, self, tid);
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
		dprintf(0, "!!! nfsfs: Corrupt stat callback, no matching token: %d\n", token);
		return;
	}

	if (status == NFS_OK) {
		cp_stats(rq->stat, attr);
	}

	syscall_reply(rq->p.tid, status_nfs2vfs(status));
	remove_request((NFS_BaseRequest *) rq);
}

/* Get file details for a specified NFS File */
void
nfsfs_stat(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf) {
	dprintf(1, "*** nfsfs_stat: %p, %s, %p\n", self, path, buf);

	if (self != NULL) {
		NFS_File *nf = (NFS_File *) self->extra;
		if (nf == NULL) {
			dprintf(0, "!!! nfsfs_stat: Broken NFS file! No nfs struct! (file %s)\n",
					path);
			syscall_reply(tid, SOS_VFS_ERROR);
			return;
		}

		memcpy((void *) buf, (void *) &(self->vstat), sizeof(stat_t));
		syscall_reply(tid, SOS_VFS_OK);
	}
	
	// stat non open file
	else {
		dprintf(1, "*** nfsfs_stat: trying to stat non open file! (file %s)\n", path);

		NFS_StatRequest *rq = (NFS_StatRequest *) create_request(RT_STAT, self, tid);
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
		dprintf(0, "!!! nfsfs: Corrupt remove callback, no matching token: %d\n", token);
		return;
	}

	syscall_reply(rq->p.tid, status_nfs2vfs(status));
	remove_request((NFS_BaseRequest *) rq);
}

/* Remove a file */
void
nfsfs_remove(L4_ThreadId_t tid, VNode self, const char *path) {
	dprintf(1, "*** nfsfs_remove: %d %s ***\n", L4_ThreadNo(tid), path);

	if (self != NULL) {
		// cant remove open files
		syscall_reply(tid, SOS_VFS_OPEN);
	} else {
		// remove file
		NFS_RemoveRequest *rq = (NFS_RemoveRequest *)
			create_request(RT_REMOVE, self, tid);
		rq->path = path;
		nfs_remove(&nfs_mnt, (char *) path, remove_cb, rq->p.token);
	}
}

