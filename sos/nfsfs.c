#include <string.h>
#include <nfs/nfs.h>

#include "nfsfs.h"

#include "constants.h"
#include "libsos.h"
#include "list.h"
#include "network.h"
#include "process.h"
#include "syscall.h"

#define verbose 1

/******** NFS TIMEOUT THREAD ********/
extern void nfs_timeout(void);

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
	pid_t pid;
};

typedef struct {
	NFS_BaseRequest p;
	fmode_t mode;
	void (*open_done) (pid_t pid, VNode self, fmode_t mode, int status);
} NFS_LookupRequest;

/* Could combine read and write since are the same, but prefer separate for
 * easy extension
 */
typedef struct {
	NFS_BaseRequest p;
	fildes_t file;
	char *buf;
	L4_Word_t pos;
	size_t nbyte;
	void (*read_done)(pid_t pid, VNode self, fildes_t file, L4_Word_t pos, char *buf,
			size_t nbyte, int status);
} NFS_ReadRequest;

typedef struct {
	NFS_BaseRequest p;
	fildes_t file;
	char *buf;
	L4_Word_t offset;
	size_t nbyte;
	void (*write_done)(pid_t pid, VNode self, fildes_t file, L4_Word_t offset,
			const char *buf, size_t nbyte, int status);
} NFS_WriteRequest;

typedef struct {
	NFS_BaseRequest p;
	stat_t *stat;
	const char *path;
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
static List *NfsRequests;

/* NFS Base directory */
static struct cookie nfs_mnt;


/* functions to run for requests */
static void rq_lookup_run(NFS_LookupRequest *rq);
static void rq_read_run(NFS_ReadRequest *brq);
static void rq_write_run(NFS_WriteRequest *brq);
static void rq_stat_run(NFS_StatRequest *brq);
static void rq_dir_run(NFS_DirRequest *brq);
static void rq_rem_run(NFS_RemoveRequest *brq);


/* Start up NFS file system */
int
nfsfs_init(void) {
	dprintf(1, "*** nfsfs_init\n");
	
	/* redefine just to limit our linkage to one place */
	nfs_mnt = mnt_point;

	NfsRequests = list_empty();
	
	/* Run the nfs time out thread */
	process_run_rootthread("nfs_timeout", nfsfs_timeout_thread, YES_TIMESTAMP);
	
	return 0;
}

/* Create a new unique token for the NFS callbacks */
static
uintptr_t
newtoken(void) {
	static uintptr_t tok = 0;
	return ((tok++) % (NULL_TOKEN - 1));
}

/* NfsRequest list search function, searching on a token */
static
int
search_requests(void *node, void *key) {
	NFS_BaseRequest *brq = (NFS_BaseRequest *) node;
	uintptr_t token = *((uintptr_t *) key);
	dprintf(2, "search_requests: %d, %d\n", brq->token, token);
	if (brq->token == token) {
		return 1;
	} else {
		return 0;
	}
}

/* Return a NFS request struct with the token specified */
static
NFS_BaseRequest*
get_request(uintptr_t token) {
	if (list_null(NfsRequests)) {
		dprintf(0, "!!! get_request: List is NULL\n");
		return NULL;
	}
	return (NFS_BaseRequest *) list_find(NfsRequests, search_requests, &token);
}

/* Run a request if its at the start of the queue */
static
int
run_request(NFS_BaseRequest *rq) {
	/* At head of queue, so run it */
	switch (rq->rt) {
		case RT_LOOKUP:
			rq_lookup_run((NFS_LookupRequest *) rq);
			break;
		case RT_READ:
			rq_read_run((NFS_ReadRequest *) rq);
			break;
		case RT_WRITE:
			rq_write_run((NFS_WriteRequest *) rq);
			break;
		case RT_STAT:
			rq_stat_run((NFS_StatRequest *) rq);
			break;
		case RT_DIR:
			rq_dir_run((NFS_DirRequest *) rq);
			break;
		case RT_REMOVE:
			rq_rem_run((NFS_RemoveRequest *) rq);
			break;
		default:
			dprintf(0, "!!! nfsfs.c: run_request: invalid request type %d\n", rq->rt);
			return 0;
	}

	return 1;
}

/* check if the specified request is at the start of the list and should run */
static
int
check_request(NFS_BaseRequest *rq) {
	if (list_null(NfsRequests)) {
		dprintf(0, "!!! nfsfs: check_request NfsRequest list is null !!!\n");
		return run_request(rq);
	}

	/* Check at head of queue */
	NFS_BaseRequest *brq = (NFS_BaseRequest *) list_peek(NfsRequests);
	if (brq != rq) {
		return 0;
	}

	return run_request((NFS_BaseRequest *) list_peek(NfsRequests));
}

/* Run request at head of queue */
static
int
run_head_request(void) {
	if (list_null(NfsRequests)) {
		return 0;
	}
	return run_request((NFS_BaseRequest *) list_peek(NfsRequests));
}


/* Create a new NFS request of type specified */
static
NFS_BaseRequest *
create_request(enum NfsRequestType rt, VNode vn, pid_t pid) {
	dprintf(2, "create request: %d %p %d\n", rt, vn, pid);
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
	rq->pid = pid;

	// add to list
	list_push(NfsRequests, rq);

	return rq;
}

/* Remove and free a specified request */
static
void
remove_request(NFS_BaseRequest *rq) {
	// remove from lists
	list_delete_first(NfsRequests, search_requests, &(rq->token));

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

	// run next request in queue
	run_head_request();
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
	// fill in compatible time stamps
	stat->st_ctime = attr->ctime.seconds * 1000 + attr->ctime.useconds / 1000;
	stat->st_atime = attr->atime.seconds * 1000 + attr->atime.useconds / 1000;
	// fill in correct time stamps
	stat->st2_ctime = (((unsigned long long) attr->ctime.seconds) * 1000)
		+ (((unsigned long long) attr->ctime.useconds) / 1000);
	stat->st2_atime = (((unsigned long long) attr->atime.seconds) * 1000)
		+ (((unsigned long long) attr->atime.useconds) / 1000);
}

/* Create the extra struct for an nfs file */
static
NFS_File *
new_nfsfile(VNode vnode) {
	if (vnode == NULL) {
		return NULL;
	}

	NFS_File *nf = (NFS_File *) malloc(sizeof(NFS_File));
	if (nf == NULL) {
		dprintf(0, "!!! new_nfsfile: malloc failed!\n");
		return NULL;
	}

	nf->vnode = vnode;
	vnode->extra = (void *) nf;

	return nf;
}

/* Free the extra struct for an nfs file */
static
void
free_nfsfile(VNode vnode) {
	if (vnode == NULL) {
		return;
	}

	free((NFS_File *) vnode->extra);
	vnode->extra = NULL;
}

/* Change an NFS Error into a VFS Error */
static
L4_Word_t
status_nfs2vfs(int status) {
	switch(status) {
		case NFS_OK:					return SOS_VFS_OK;
		case NFSERR_ROFS:
		case NFSERR_ACCES:
		case NFSERR_PERM:				return SOS_VFS_PERM;
		case NFSERR_NOENT:			return SOS_VFS_NOVNODE;
		case NFSERR_NAMETOOLONG:
		case NFSERR_NODEV:         return SOS_VFS_PATHINV;
		case NFSERR_ISDIR:         return SOS_VFS_DIR;
		case NFSERR_EXIST:         return SOS_VFS_EXIST;
		case NFSERR_STALE:         return SOS_VFS_CORVNODE;
		case NFSERR_IO:
		case NFSERR_NXIO:
		case NFSERR_NOTDIR:
		case NFSERR_FBIG:
		case NFSERR_NOSPC:
		case NFSERR_NOTEMPTY:
		case NFSERR_DQUOT:
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
		dprintf(2, "nfsfs: Sending: %d, %d\n", token, rq->p.pid);
		rq->open_done(rq->p.pid, rq->p.vnode, rq->mode, SOS_VFS_OK);
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
		free_nfsfile(rq->p.vnode);
		rq->open_done(rq->p.pid, rq->p.vnode, rq->mode, status_nfs2vfs(status));
		remove_request((NFS_BaseRequest *) rq);
	}
}

/* Open a specified file using NFS */
void
nfsfs_open(pid_t pid, VNode self, const char *path, fmode_t mode,
		void (*open_done)(pid_t pid, VNode self, fmode_t mode, int status)) {
	dprintf(1, "*** nfsfs_open: %p, %s, %d, %p\n", self, path, mode, open_done);

	memcpy( (void *) self->path, (void *) path, MAX_FILE_NAME);
	self->readers = 0;
	self->writers = 0;
	self->vstat.st_type = ST_FILE;
	self->next = NULL;
	self->previous = NULL;

	if (new_nfsfile(self) == NULL) {
		dprintf(0, "!!! nfsfs_open: malloc failed!\n");
		open_done(pid, self, mode, SOS_VFS_NOMEM);
		return;
	}

	self->open = nfsfs_open;
	self->close = nfsfs_close;
	self->read = nfsfs_read;
	self->write = nfsfs_write;
	self->flush = nfsfs_flush;
	self->getdirent = nfsfs_getdirent;
	self->stat = nfsfs_stat;
	self->remove = nfsfs_remove;

	NFS_LookupRequest *rq = (NFS_LookupRequest *) create_request(RT_LOOKUP, self, pid);
	rq->mode = mode;
	rq->open_done = open_done;
	
	check_request((NFS_BaseRequest *) rq);
}

/* Run the actual open/lookup request */
static
void
rq_lookup_run(NFS_LookupRequest *rq) {
	dprintf(2, "run NFS Lookup request\n");
	// If open mode is write, then create new file since we want to start again.
	if (rq->mode & FM_WRITE && !(rq->mode & FM_NOTRUNC)) {
		sattr_t sat = DEFAULT_SATTR;
		nfs_create(&nfs_mnt, rq->p.vnode->path, &sat, lookup_cb, rq->p.token);
	} else {
		nfs_lookup(&nfs_mnt, rq->p.vnode->path, lookup_cb, rq->p.token);
	}
}

/* Close a specified file previously opened with nfsfs_open, don't free the vnode
 * just free nfs specific file structs as vfs will free the vnode
 */
void
nfsfs_close(pid_t pid, VNode self, fildes_t file, fmode_t mode,
		void (*close_done)(pid_t pid, VNode self, fildes_t file, fmode_t mode, int status)) {
	dprintf(1, "*** nfsfs_close: %p, %d, %d, %p\n", self, file, mode, close_done);

	if (self == NULL) {
		dprintf(0, "!!! nfsfs_close: Trying to close null file!\n");
		close_done(pid, self, file, mode, SOS_VFS_NOFILE);
		return;
	}

	free_nfsfile(self);
	close_done(pid, self, file, mode, SOS_VFS_OK);
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
		rq->read_done(rq->p.pid, rq->p.vnode, rq->file, 0, rq->buf, 0, status_nfs2vfs(status));
		remove_request((NFS_BaseRequest *) rq);
		return;
	}

	memcpy((void *) rq->buf, (void *) data, bytes_read);
	cp_stats(&(rq->p.vnode->vstat), attr);

	// call vfs to handle fp and anything else
	rq->read_done(rq->p.pid, rq->p.vnode, rq->file, 0, rq->buf, bytes_read, bytes_read);
	remove_request((NFS_BaseRequest *) rq);
}

/* Read a specified number of bytes into a buffer from the given NFS file */
void
nfsfs_read(pid_t pid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, void (*read_done)(pid_t pid,
			VNode self, fildes_t file, L4_Word_t pos, char *buf, size_t nbyte, int status)) {
	dprintf(1, "*** nfsfs_read: %p, %d, %d, %p, %d\n", self, file, pos, buf, nbyte);

	NFS_File *nf = (NFS_File *) self->extra;	
	if (self == NULL || nf == NULL) {
		dprintf(0, "!!! nfsfs_read: Invalid NFS file (p %d, f %d), no nfs struct!\n",
				pid, file);
		read_done(pid, self, file, pos, buf, 0, SOS_VFS_NOFILE);
		return;
	}

	NFS_ReadRequest *rq = (NFS_ReadRequest *) create_request(RT_READ, self, pid);
	rq->file = file;
	rq->buf = buf;
	rq->pos = pos;
	rq->nbyte = nbyte;
	rq->read_done = read_done;

	check_request((NFS_BaseRequest *) rq);
}

/* Run the actual read request */
static
void
rq_read_run(NFS_ReadRequest *rq) {
	dprintf(2, "run NFS Read request\n");
	NFS_File *nf = (NFS_File *) rq->p.vnode->extra;
	nfs_read(&(nf->fh), rq->pos, rq->nbyte, read_cb, rq->p.token);
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
		rq->write_done(rq->p.pid, rq->p.vnode, rq->file, 0, rq->buf, 0, status_nfs2vfs(status));
		remove_request((NFS_BaseRequest *) rq);
		return;
	}

	cp_stats(&(rq->p.vnode->vstat), attr);

	// call vfs to handle fp and anything else
	rq->write_done(rq->p.pid, rq->p.vnode, rq->file, 0, rq->buf, rq->nbyte, rq->nbyte);
	remove_request((NFS_BaseRequest *) rq);
}

/* Write the specified number of bytes from the buffer buf to a given NFS file */
void
nfsfs_write(pid_t pid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, void (*write_done)(pid_t pid, VNode self,
			fildes_t file, L4_Word_t offset, const char *buf, size_t nbyte, int status)) {
	dprintf(1, "*** nfsfs_write: %p, %d, %d, %p, %d\n", self, file, offset, buf, nbyte);

	NFS_File *nf = (NFS_File *) self->extra;	
	if (nf == NULL) {
		dprintf(0, "!!! nfsfs_write: Invalid NFS file (p %d, f %d), no nfs struct!\n",
				pid, file);
		write_done(pid, self, file, offset, buf, 0, SOS_VFS_NOFILE);
		return;
	}

	NFS_WriteRequest *rq = (NFS_WriteRequest *) create_request(RT_WRITE, self, pid);
	rq->file = file;
	rq->buf = (char *) buf;
	rq->offset = offset;
	rq->nbyte = nbyte;
	rq->write_done = write_done;

	check_request((NFS_BaseRequest *) rq);
}

/* Run the actual write request */
static
void
rq_write_run(NFS_WriteRequest *rq) {
	dprintf(2, "run NFS Write request\n");
	NFS_File *nf = (NFS_File *) rq->p.vnode->extra;
	nfs_write(&(nf->fh), rq->offset, rq->nbyte, rq->buf, write_cb, rq->p.token);
}

/* Flush the given nfs file to disk. (UNSUPPORTED) (no buffering used) */
void
nfsfs_flush(pid_t pid, VNode self, fildes_t file) {
	dprintf(1, "*** nfsfs_flush: %d, %p, %d\n", pid, self, file);
	dprintf(0, "!!! nfsfs_flush: Not implemented for nfs fs\n");
	syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_NOTIMP);
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

	Process *p = process_lookup(rq->p.pid);

	if (status != NFS_OK) {
		syscall_reply(process_get_tid(p), status_nfs2vfs(status));
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
		syscall_reply(process_get_tid(p), status);
		remove_request((NFS_BaseRequest *) rq);
	}
	// need later directory entry
	else if (next_cookie > 0) {
		dprintf(2, "Need more dir entries to get file\n");
		rq->cpos += num_entries;
		nfs_readdir(&nfs_mnt, next_cookie, IO_MAX_BUFFER, getdirent_cb, rq->p.token);
	}
	// error case, just return SOS_VFS_OK to say nothing read, its not an error just eof
	else {
		dprintf(2, "nfsfs_getdirent: didnt find file (%d)\n", rq->pos);
		syscall_reply(process_get_tid(p), SOS_VFS_EOF);
		remove_request((NFS_BaseRequest *) rq);
	}
}

/* Get directory entries of the NFS filesystem */
void
nfsfs_getdirent(pid_t pid, VNode self, int pos, char *name, size_t nbyte) {
	dprintf(1, "*** nfsfs_getdirent: %p, %d, %p, %d\n", self, pos, name, nbyte);

	NFS_DirRequest *rq = (NFS_DirRequest *) create_request(RT_DIR, self, pid);
	rq->pos = pos;
	rq->buf = name;
	rq->nbyte = nbyte;
	rq->cpos = 0;

	check_request((NFS_BaseRequest *) rq);
}

/* Run a getdirent request */
static
void
rq_dir_run(NFS_DirRequest *rq) {
	dprintf(2, "run NFS getdirent request\n");
	nfs_readdir(&nfs_mnt, 0, IO_MAX_BUFFER, getdirent_cb, rq->p.token);
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

	syscall_reply(process_get_tid(process_lookup(rq->p.pid)),
			status_nfs2vfs(status));
	remove_request((NFS_BaseRequest *) rq);
}

/* Get file details for a specified NFS File */
void
nfsfs_stat(pid_t pid, VNode self, const char *path, stat_t *buf) {
	dprintf(1, "*** nfsfs_stat: %p, %s, %p\n", self, path, buf);

	Process *p = process_lookup(pid);

	if (self != NULL) {
		NFS_File *nf = (NFS_File *) self->extra;
		if (nf == NULL) {
			dprintf(0, "!!! nfsfs_stat: Broken NFS file! No nfs struct! (file %s)\n",
					path);
			syscall_reply(process_get_tid(p), SOS_VFS_ERROR);
			return;
		}

		memcpy((void *) buf, (void *) &(self->vstat), sizeof(stat_t));
		syscall_reply(process_get_tid(p), SOS_VFS_OK);
	}
	
	// stat non open file
	else {
		dprintf(1, "*** nfsfs_stat: trying to stat non open file! (file %s)\n", path);

		NFS_StatRequest *rq = (NFS_StatRequest *) create_request(RT_STAT, self, pid);
		rq->stat = buf;
		rq->path = path;
		check_request((NFS_BaseRequest *) rq);
	}
}

/* Run a stat request */
static
void
rq_stat_run(NFS_StatRequest *rq) {
	dprintf(2, "run NFS Stat request\n");
	nfs_lookup(&nfs_mnt, (char *) rq->path, stat_cb, rq->p.token);
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

	syscall_reply(process_get_tid(process_lookup(rq->p.pid)),
			status_nfs2vfs(status));
	remove_request((NFS_BaseRequest *) rq);
}

/* Remove a file */
void
nfsfs_remove(pid_t pid, VNode self, const char *path) {
	dprintf(1, "*** nfsfs_remove: %d %s ***\n", pid, path);

	if (self != NULL) {
		// cant remove open files
		syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_OPEN);
	} else {
		// remove file
		NFS_RemoveRequest *rq = (NFS_RemoveRequest *)
			create_request(RT_REMOVE, self, pid);
		rq->path = path;
		check_request((NFS_BaseRequest *) rq);
	}
}

static
void
rq_rem_run(NFS_RemoveRequest *rq) {
	dprintf(2, "run NFS Remove request\n");
	nfs_remove(&nfs_mnt, (char *) rq->path, remove_cb, rq->p.token);
}

