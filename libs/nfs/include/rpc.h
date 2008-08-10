/* these are RPC and NFS struct definitions */

#ifndef __RPC_H
#define __RPC_H

// #include "nfs.h"

/* this is general */
#define SETBUF_LEN 1024
typedef struct setbuf
{
	char buf[ SETBUF_LEN ];
	int pos;
	int len;
} setbuf_t;

/****************************************************
*
* This is the RPC section
*
****************************************************/

/*
         enum msg_type {
            CALL  = 0,
            REPLY = 1
         };
*/

#define MSG_CALL	0
#define MSG_REPLY	1



/*
         enum reply_stat {
            MSG_ACCEPTED = 0,
            MSG_DENIED   = 1
         };

*/

#define MSG_ACCEPTED 0
#define MSG_DENIED   1


/*
         enum accept_stat {
            SUCCESS       = 0, // RPC executed successfully
            PROG_UNAVAIL  = 1, // remote hasn't exported program
            PROG_MISMATCH = 2, // remote can't support version #
            PROC_UNAVAIL  = 3, // program can't support procedure
            GARBAGE_ARGS  = 4  // procedure can't decode params
         };
*/

#define SUCCESS        0
#define PROG_UNAVAIL   1
#define PROG_MISMATCH  2
#define PROC_UNAVAIL   3
#define GARBAGE_ARGS   4



/*
         struct rpc_msg {
            unsigned int xid;
            union switch (msg_type mtype) {
            case CALL:
               call_body cbody;
            case REPLY:
               reply_body rbody;
            } body;
         };
*/

typedef unsigned int xid_t;

/*
	struct call_body 
	{ 
		unsigned int rpcvers; // Must be two (2)
		unsigned int prog; 
		unsigned int vers; 
		unsigned int proc; 
		opaque_auth cred; 
		opaque_auth verf; 

		// procedure specific parameters start here
	};
*/


#define AUTH_NULL	0
#define AUTH_UNIX	1
#define AUTH_SHORT	2
#define AUTH_DES	3
            

typedef struct opaque_auth
{
	unsigned int flavour;
	unsigned int size;

	/* you can put other stuff after here, too! */
} opaque_auth_t;

typedef struct 
{ 
	unsigned int rpcvers; // Must be two (2)
	unsigned int prog; 
	unsigned int vers; 
	unsigned int proc; 

/*
	opaque_auth_t cred; 
	opaque_auth_t verf; 
*/

	// procedure specific parameters start here
} call_body;




/*
         union reply_body switch (reply_stat stat) 
		 {
			case MSG_ACCEPTED:
				accepted_reply areply;
			case MSG_DENIED:
				rejected_reply rreply;
         } reply;
*/

typedef unsigned int reply_stat;

/*
	struct accepted_reply 
	{ 
		opaque_auth verf; 
		union switch (accept_stat stat) 
		{ 
			case SUCCESS: 
				opaque results[0]; 
				// * procedure-specific results start here 
			case PROG_MISMATCH: 
				struct 
				{ 
					unsigned int low; 
					unsigned int high; 
				} mismatch_info; 
			default: 
				// * Void. Cases include PROG_UNAVAIL, PROC_UNAVAIL, * and GARBAGE_ARGS.  
				void; 
		} reply_data; 
	}; 
*/

/* typedef accept_stat stat; */

#define SRPC_VERSION 2
/****************************************************
*
* This is the portmapper section
*
****************************************************/

/* we will only implement the getmap funciton */
#define PMAP_PORT     111      /* portmapper port number */

#define PMAP_NUMBER   100000
#define PMAP_VERSION  2


/*
 * Structs
 */

typedef struct mapping 
{
	unsigned int prog;
	unsigned int vers;
	unsigned int prot;
	unsigned int port;
} mapping_t;


/* we might need to uncomment these later! */
#define IPPROTO_TCP 6      /* protocol number for TCP/IP */
#define IPPROTO_UDP 17     /* protocol number for UDP/IP */


/*
 * Functions
 */

/* unsigned int PMAPPROC_GETPORT(mapping)   = 3; */
#define PMAPPROC_GETPORT 3
unsigned int map_getport( mapping_t* pmap );



/****************************************************
*
* This is the mountd section
*
****************************************************/
#define MNT_NUMBER    100005
#define MNT_VERSION   1


/* file handle */
#define FHSIZE	32
struct cookie
{
	char data[FHSIZE];
};

/* exportlist MNTPROC_EXPORT(void) = 5; */
#define MNTPROC_EXPORT	5

/* fhstatus MNTPROC_MNT(dirpath) = 1; */
#define MNTPROC_MNT		1


/****************************************************
*
* This is the NFS section
*
****************************************************/

#define NFS_NUMBER    100003
#define NFS_VERSION   2


/* status */
#define NFS_OK			0
#define NFSERR_PERM		1
#define NFSERR_NOENT		2
#define NFSERR_IO		5
#define NFSERR_NXIO		6
#define NFSERR_ACCES		13
#define NFSERR_EXIST		17
#define NFSERR_NODEV		19
#define NFSERR_NOTDIR		20
#define NFSERR_ISDIR		21
#define NFSERR_FBIG		27
#define NFSERR_NOSPC		28
#define NFSERR_ROFS		30
#define NFSERR_NAMETOOLONG	63
#define NFSERR_NOTEMPTY		66
#define NFSERR_DQUOT		69
#define NFSERR_STALE		70
#define NFSERR_WFLUSH		99

/* functions */
#define NFSPROC_NULL 0

struct nfs_filename {
	int size;
	const char *file;
};

#define COOKIE_START 0

typedef int nfscookie_t;


typedef struct readdirargs
{
	struct cookie dir;
	nfscookie_t cookie;
	unsigned int count;
} readdirargs_t;

/* readdirres NFSPROC_READDIR (readdirargs) = 16; */
#define NFSPROC_READDIR 16

/*
struct diropargs 
{
	fhandle  dir;
	filename name;
};


union diropres switch (stat status) 
{
	case NFS_OK:
		struct 
		{
			fhandle file;
            fattr   attributes;
		} diropok;
    default:
		void;
};

*/

typedef enum ftype 
{
	NFNON = 0,
	NFREG = 1,
	NFDIR = 2,
	NFBLK = 3,
	NFCHR = 4,
	NFLNK = 5
} ftype_t;


typedef struct timeval2
{
	unsigned int seconds;
	unsigned int useconds;
} timeval_t;


typedef struct fattr 
{
	ftype_t      type;
	unsigned int mode;
	unsigned int nlink;
	unsigned int uid;
	unsigned int gid;
	unsigned int size;
	unsigned int blocksize;
	unsigned int rdev;
	unsigned int blocks;
	unsigned int fsid;
	unsigned int fileid;
	timeval_t    atime;
	timeval_t    mtime;
	timeval_t    ctime;
} fattr_t;


#define NFSPROC_GETATTR 1

/* diropres NFSPROC_LOOKUP(diropargs)       = 4; */
#define NFSPROC_LOOKUP 4

/*
struct createargs {
    diropargs where;
    sattr attributes;
};
*/

typedef struct sattr 
{
    unsigned int mode;
    unsigned int uid;
    unsigned int gid;
    unsigned int size;
    timeval_t    atime;
    timeval_t    mtime;
} sattr_t;

/* diropres NFSPROC_CREATE(createargs) = 9; */
#define NFSPROC_CREATE 9


typedef struct readargs 
{
	struct cookie file;
	unsigned offset;
	unsigned count;
	unsigned totalcount;

} readargs_t;

/*
union readres switch (stat status)
{
	case NFS_OK:
	fattr attributes;
	nfsdata data;
	default:
	void;
};
*/

/* readres NFSPROC_READ(readargs) = 6; */
#define NFSPROC_READ 6


typedef struct writeargs 
{
    struct cookie file;
    int beginoffset;
    int offset;
    int totalcount;

    /* nfsdata data; */
} writeargs_t;

/* attrstat NFSPROC_WRITE(writeargs) = 8; */
#define NFSPROC_WRITE 8


#endif /* __RPC_H */
