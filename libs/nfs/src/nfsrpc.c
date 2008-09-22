#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <nfs/nfs.h>
#include "transport.h"

//#define DEBUG_NFS 0
#ifdef DEBUG_NFS
	#define debug(x...) printf(x)
#else
	#define debug(x...)
#endif

static int nfs_port = 0;
static int mount_port = 0;

static int check_errors(struct pbuf *pbuf);

/********************************************************
*
*  Port mapper Functions
*
*********************************************************/

unsigned int
map_getport(mapping_t* pmap)
{
    int port;
    struct pbuf *pbuf;
    struct pbuf *ret;
    debug("Getting port\n");
    pmap->port = 0;
    
    /* add the xid */
    pbuf = initbuf(PMAP_NUMBER, PMAP_VERSION, PMAPPROC_GETPORT);
    
    /* pack up the map struct */
    addtobuf(pbuf, (char*) pmap, sizeof(mapping_t));
    
    /* make the call */
    ret = rpc_call(pbuf, PMAP_PORT);

    assert(ret != NULL);
    
    /* now we can extract the port */
    getfrombuf(ret, (char*) &port, sizeof(port));
    
    pmap->port = port;
    
    debug("Got port %d\n", port);

    return 0;
}

/********************************************************
*
*  Mount protocol mapper Functions
*
*********************************************************/

unsigned int
mnt_get_export_list(void)
{
    struct pbuf *pbuf;
    struct pbuf *ret;
    char str[100];

    int opt;

    pbuf = initbuf(MNT_NUMBER, MNT_VERSION, MNTPROC_EXPORT);

    ret = rpc_call(pbuf, mount_port);

    while (getfrombuf(ret, (char*) &opt, sizeof(opt)), opt) {

	printf( "NFS Export...\n" );

	getstring(ret, str, 100);

	printf( "* Export name is %s\n", (char*) &str );

	/* now to extract more stuff... */
	while (getfrombuf(ret, (char*) &opt, sizeof(opt)), opt) {
	    getstring(ret, str, 100 );
	    printf("* Group %s\n", (char*) str);
	}
    }

    return 0;
}

unsigned int
mnt_mount(char *dir, struct cookie *pfh)
{
    struct pbuf *pbuf, *ret;
    int status;

    pbuf = initbuf(MNT_NUMBER, MNT_VERSION, MNTPROC_MNT);

    addstring(pbuf, dir);
    
    ret = rpc_call(pbuf, mount_port);

	 if (ret == 0) {
		 debug( "mount call failed :(\n" );
		 return 1;
	 }

    /* now we do some stuff :) */
    getfrombuf(ret, (char*) &status, sizeof(status));

	 if (status != 0) {
		 debug( "Could not mount %s, %d!\n", dir, status );
		 return 1;
	 }

	 debug("All seems good for mount: %s!\n", dir);

    getfrombuf(ret, (char*) pfh, sizeof(struct cookie));

    return 0;
}

/********************************************************
*  Syncronous NFS Functions
*********************************************************/

/* this should be called once at beginning to setup everything */
int
nfs_init(struct ip_addr server)
{
	mapping_t map;

	init_transport(server);

	/* make and RPC to get mountd info */
	map.prog = MNT_NUMBER;
	map.vers = MNT_VERSION;
	map.prot = IPPROTO_UDP;

	if (map_getport(&map) == 0) {
		debug("mountd port number is %d\n", map.port);
		mount_port = map.port;
		if (mount_port == 0) {
			printf("Mount port invalid\n");
			return 1;
		}
	} else {
		printf("Error getting mountd port number\n");
		return 1;
	}

	/* make and RPC to get nfs info */
	map.prog = NFS_NUMBER;
	map.vers = NFS_VERSION;
	map.prot = IPPROTO_UDP;

	if(map_getport(&map) == 0) {
		debug( "nfs port number is %d\n", map.port );
		nfs_port = map.port;
	} else {
		if (nfs_port == 0) {
			printf("Invalid NFS port\n");
			return 1;
		}
		debug( "Error getting NFS port number\n" );
		return 1;
	}

	return 0;
}

/******************************************
 * Async functions
 ******************************************/

void
nfs_getattr_cb(void * callback, uintptr_t token, struct pbuf *pbuf)
{
    int err = 0, status = -1;
    fattr_t pattrs;
    void (*cb) (uintptr_t, int, fattr_t *) = callback;

    assert(callback != NULL);

    err = check_errors(pbuf);

    if (err == 0) {
	/* get the status out */
	getfrombuf(pbuf, (char*) &status, sizeof(status));

	if (status == NFS_OK) {
	    /* it worked, so take out the return stuff! */
	    getfrombuf(pbuf, (void*) &pattrs, 
		   sizeof(fattr_t));
	}
    }

    cb(token, status, &pattrs);

    return;
}

int
nfs_getattr(struct cookie *fh,
       void (*func) (uintptr_t, int, fattr_t *), 
       uintptr_t token)
{
    struct pbuf *pbuf;

    /* now the user data struct is setup, do some call stuff! */
    pbuf = initbuf(NFS_NUMBER, NFS_VERSION, NFSPROC_GETATTR);
    
    /* put in the fhandle */
    addtobuf(pbuf, (char*) fh, sizeof(struct cookie));
    
    /* send it! */
    return rpc_send(pbuf, nfs_port, nfs_getattr_cb, func, token);
}

void
nfs_lookup_cb(void * callback, uintptr_t token, struct pbuf *pbuf)
{
    int err = 0, status = -1;
    struct cookie new_fh;
    fattr_t pattrs;
    void (*cb) (uintptr_t, int, struct cookie *, fattr_t *) = callback;

    assert(callback != NULL);

    err = check_errors(pbuf);

    if (err == 0) {
	/* get the status out */
	getfrombuf(pbuf, (char*) &status, sizeof(status));

	if (status == NFS_OK) {
	    /* it worked, so take out the return stuff! */
	    getfrombuf(pbuf, (void*) &new_fh, 
		   sizeof(struct cookie));
	    getfrombuf(pbuf, (void*) &pattrs, 
		   sizeof(fattr_t));
	}
    }

    cb(token, status, &new_fh, &pattrs);

    return;
}

/* request a file handle */
int
nfs_lookup(struct cookie *cwd, char *name, 
       void (*func) (uintptr_t, int, struct cookie *, fattr_t *), 
       uintptr_t token)
{
    struct pbuf *pbuf;

    /* now the user data struct is setup, do some call stuff! */
    pbuf = initbuf(NFS_NUMBER, NFS_VERSION, NFSPROC_LOOKUP);
    
    /* put in the fhandle */
    addtobuf(pbuf, (char*) cwd, sizeof(struct cookie));
    
    /* put in the name */
    addstring(pbuf, name);
    
    /* send it! */
    return rpc_send(pbuf, nfs_port, nfs_lookup_cb, func, token);
}

void
nfs_read_cb(void * callback, uintptr_t token, struct pbuf *pbuf)
{
    int err = 0, status = -1;
    fattr_t pattrs;
    char *data = NULL;
    int size = 0;
    void (*cb) (uintptr_t, int, fattr_t *, int, char *) = callback;

    err = check_errors(pbuf);

    assert(callback != NULL);

    if (err == 0) {
	/* get the status out */
	getfrombuf(pbuf, (char*) &status, sizeof(status));

	if (status == NFS_OK) {
	    /* it worked, so take out the return stuff! */
	    getfrombuf(pbuf, (void*) &pattrs, 
		   sizeof(fattr_t));
	    getfrombuf(pbuf, (void*) &size, 
		   sizeof(int));
	    data = getpointfrombuf(pbuf, pattrs.size);
	}
    }

    cb(token, status, &pattrs, size, data);

    return;
}

int
nfs_read(struct cookie *fh, int pos, int count,
     void (*func) (uintptr_t, int, fattr_t *, int, char *),
     uintptr_t token)
{
    struct pbuf *pbuf;
    readargs_t args;
    
    /* now the user data struct is setup, do some call stuff! */
    pbuf = initbuf(NFS_NUMBER, NFS_VERSION, NFSPROC_READ);

    /* copy in the fhandle */
    memcpy(&args.file, (char*) fh, sizeof(struct cookie));
    
    args.offset = pos;
    args.count = count;
    args.totalcount = 0;  /* unused as per RFC */
    
    /* add them to the buffer */
    addtobuf(pbuf, (char*) &args, sizeof(args));
    
    return rpc_send(pbuf, nfs_port, nfs_read_cb, func, token);
}

void
nfs_write_cb(void * callback, uintptr_t token, struct pbuf *pbuf)
{
    int err = 0, status = -1;
    fattr_t pattrs;
    void (*cb) (uintptr_t, int, fattr_t *) = callback;

    err = check_errors(pbuf);

    assert(callback != NULL);

    if (err == 0) {
	/* get the status out */
	getfrombuf(pbuf, (char*) &status, sizeof(status));

	if (status == NFS_OK) {
	    /* it worked, so take out the return stuff! */
	    getfrombuf(pbuf, (void*) &pattrs, 
		   sizeof(fattr_t));
	}
    }

    cb(token, status, &pattrs);

    return;
}

int
nfs_write(struct cookie *fh, int offset, int count, void *data,
     void (*func) (uintptr_t, int, fattr_t *),
     uintptr_t token)
{
    struct pbuf *pbuf;
    writeargs_t args;
    
    /* now the user data struct is setup, do some call stuff! */
    pbuf = initbuf(NFS_NUMBER, NFS_VERSION, NFSPROC_WRITE);

    /* copy in the fhandle */
    memcpy(&args.file, (char*) fh, sizeof(struct cookie));
    
    args.offset = offset;
    args.beginoffset = 0; /* unused as per RFC */
    args.totalcount = 0;  /* unused as per RFC */
    
    /* add them to the buffer */
    addtobuf(pbuf, (char*) &args, sizeof(args));
    
    /* put the data in */
    adddata(pbuf, data, count);

    return rpc_send(pbuf, nfs_port, nfs_write_cb, func, token);
}

void
nfs_create_cb(void * callback, uintptr_t token, struct pbuf *pbuf)
{
    int err = 0, status = -1;
    struct cookie new_fh;
    fattr_t pattrs;
    void (*cb) (uintptr_t, int, struct cookie *, fattr_t *) = callback;

    assert(callback != NULL);

    err = check_errors(pbuf);

    if (err == 0) {
	/* get the status out */
	getfrombuf(pbuf, (char*) &status, sizeof(status));

	if (status == NFS_OK) {
	    /* it worked, so take out the return stuff! */
	    getfrombuf(pbuf, (void*) &new_fh, 
		   sizeof(struct cookie));
	    getfrombuf(pbuf, (void*) &pattrs, 
		   sizeof(fattr_t));
	}
    }

    debug("NFS CREATE CALLBACK\n");
    cb(token, status, &new_fh, &pattrs);

    return;

}

int
nfs_create(struct cookie *fh, char *name, sattr_t *sat,
     void (*func) (uintptr_t, int, struct cookie *, fattr_t *),
     uintptr_t token)
{
    struct pbuf *pbuf;
    
    /* now the user data struct is setup, do some call stuff! */
    pbuf = initbuf(NFS_NUMBER, NFS_VERSION, NFSPROC_CREATE);

    /* put in the fhandle */
    addtobuf(pbuf, (char*) fh, sizeof(struct cookie));
    
    /* put in the name */
    addstring(pbuf, name);
    
    addtobuf(pbuf, (char*) sat, sizeof(sattr_t));

    return rpc_send(pbuf, nfs_port, nfs_create_cb, func, token);
}

void
nfs_remove_cb(void * callback, uintptr_t token, struct pbuf *pbuf)
{
	int err = 0, status = -1;
	void (*cb) (uintptr_t, int) = callback;

	assert(callback != NULL);

	err = check_errors(pbuf);

	if (err == 0) {
		/* get the status out */
		getfrombuf(pbuf, (char*) &status, sizeof(status));
	}

	cb(token, status);

	return;
}

/* remove a file named 'name' in directory 'cwd' */
int
nfs_remove(struct cookie *cwd, char *name, 
       void (*func) (uintptr_t, int), uintptr_t token)
{
    struct pbuf *pbuf;

    /* now the user data struct is setup, do some call stuff! */
    //pbuf = initbuf(NFS_NUMBER, NFS_VERSION, NFSPROC_REMOVE);
    pbuf = initbuf(NFS_NUMBER, NFS_VERSION, 10);
    
    /* put in the fhandle */
    addtobuf(pbuf, (char*) cwd, sizeof(struct cookie));
    
    /* put in the name */
    addstring(pbuf, name);
    
    /* send it! */
    return rpc_send(pbuf, nfs_port, nfs_remove_cb, func, token);
}


int
getentries_readdir(struct pbuf *pbuf, int *cookie)
{
    void *old_arg_0 = pbuf->arg[0];
    int tmp = 1;
    int count = 0;
    int fileid;

    getfrombuf(pbuf, (char*) &tmp, sizeof(tmp));
    debug("Got entry: %d\n", tmp);
    
    while(tmp) {
	getfrombuf(pbuf, (char*) &fileid, sizeof(fileid));
	skipstring(pbuf);
	debug("Skipped string\n");
	getfrombuf(pbuf, (char*) cookie, sizeof(int));
	debug("Skipped string %d\n", *cookie);
	count++;
	getfrombuf(pbuf, (char*) &tmp, sizeof(tmp));
    }
    
    getfrombuf(pbuf, (char*) &tmp, sizeof(tmp));
    
    if (tmp == 0)
	cookie = 0;

    pbuf->arg[0] = old_arg_0;

    debug("Returning: %d\n", count);

    return count;
}

void
nfs_readdir_cb(void * callback, uintptr_t token, struct pbuf *pbuf)
{
    int err = 0, status = -1, num_entries = 0, next_cookie = 0;
    struct nfs_filename *entries = NULL;
    void (*cb) (uintptr_t , int, int, struct nfs_filename *, int) = callback;
    int count = 0;

    debug("NFS READDIR CALLBACK\n");

    assert(callback != NULL);

    err = check_errors(pbuf);

    if (err == 0) {
	/* get the status out */
	getfrombuf(pbuf, (char*) &status, sizeof(status));

	if (status == NFS_OK) {
	    int tmp, fileid, cookie;
	    debug("Getting entries\n");
	    num_entries = getentries_readdir(pbuf, &next_cookie);
	    entries = malloc(sizeof(struct nfs_filename) * 
		     num_entries);

	    
	    getfrombuf(pbuf, (char*) &tmp, sizeof(tmp));
	    debug("Got entry: %d\n", tmp);
	    
	    while(tmp) {
		int size;
		getfrombuf(pbuf, (char*) &fileid, 
		       sizeof(fileid));
		debug("Got filed: %d\n", fileid);
		getfrombuf(pbuf, (char*) &entries[count].size,
		       sizeof(int));
		debug("Got size: %d\n", entries[count].size);
		entries[count].file = pbuf->arg[0];
		size = entries[count].size;
		if (size % 4)
		    size += 4 - (size % 4);
		pbuf_adv_arg(pbuf, 0, size);
		debug("Got size: %p\n", pbuf->arg[0]);
		
		getfrombuf(pbuf, (char*) &cookie, sizeof(int));
		
		count++;
		getfrombuf(pbuf, (char*) &tmp, sizeof(tmp));
	    }
	}
    }

    cb(token, status, num_entries, entries, next_cookie);
    if (entries)
	free(entries);

    return;

}

/* send a request for a directory item */
int
nfs_readdir(struct cookie *pfh, int cookie, int size,
	void (*func) (uintptr_t , int, int, struct nfs_filename *, int),
	uintptr_t token)
{
    readdirargs_t args;

    struct pbuf *pbuf;
    
    /* now the user data struct is setup, do some call stuff! */
    pbuf = initbuf(NFS_NUMBER, NFS_VERSION, NFSPROC_READDIR);

    /* copy the buffer */
    memcpy(&args.dir, pfh, sizeof(struct cookie));
    
    /* set the cookie */
    args.cookie = cookie;
    args.count = size;

    /* copy the arguments into the packet */
    addtobuf(pbuf, (char*) &args, sizeof(args));

    /* make the call! */
    return rpc_send(pbuf, nfs_port, nfs_readdir_cb, func, token);
}

/********************************************
 * Data extraction functions
 ********************************************/

static int
check_errors(struct pbuf *pbuf)
{
    xid_t txid;
    int ctype;
    opaque_auth_t auth;
    int r;
    
    /* extract the xid */
    getfrombuf(pbuf, (char*) &txid, sizeof(txid));
    
    /* and the call type */
    getfrombuf(pbuf, (char*) &ctype, sizeof(ctype));
    
    
    if (ctype != MSG_REPLY) {
	debug( "Got a reply to something else!!\n" );
	
	debug( "Looking for msgtype %d\n", MSG_REPLY );
	debug( "Got msgtype %d\n", ctype );
	
	return ERR_BAD_MSG;
    }
    
    /* check if it was an accepted reply */
    getfrombuf(pbuf, (char*) &r, sizeof(r));
    
    if (r != MSG_ACCEPTED) {
	debug( "Message NOT accepted (%d)\n", r );
	
	/* extract error code */
	getfrombuf( pbuf, (char*) &r, sizeof( r ) );
	debug( "Error code %d\n", r );
	
	if (r == 1) {
	    /* get the auth problem */
	    getfrombuf( pbuf, (char*) &r, sizeof( r ) );
	    debug( "auth_stat %d\n", r );
	}
	
	return ERR_NOT_ACCEPTED;
    }
    
    /* and the auth data!*/
    getfrombuf(pbuf, (char*) &auth, sizeof(auth));
    
    if (auth.flavour != AUTH_NULL)
	assert("gave back other auth type!\n");
    
    /* check its accept stat */
    getfrombuf(pbuf, (char*) &r, sizeof(r));
    
    if (r == SUCCESS) {
	return 0;
    } else {
	debug( "reply stat was %d\n", r );
	return ERR_FAILURE;
    }
}
