#ifndef __NFS_H
#define __NFS_H

#include <stdint.h>
#include <lwip/api.h>

#include <nfs/rpc.h>

/* to initialise the lot */
int nfs_init(struct ip_addr server);

/* mount functions */
unsigned int mnt_get_export_list(void);
unsigned int mnt_mount(char* dir, struct cookie *pfh);

/* NFS functions */
int nfs_getattr(struct cookie *fh,
		void (*func) (uintptr_t, int, fattr_t *), 
		uintptr_t token);

int nfs_lookup(struct cookie *cwd, char *name, 
	       void (*func) (uintptr_t, int, struct cookie *, fattr_t *), 
	       uintptr_t token);

int nfs_create(struct cookie *fh, char *name, sattr_t *sat,
	       void (*func) (uintptr_t, int, struct cookie *, fattr_t *),
	       uintptr_t token);

int nfs_read(struct cookie *fh, int pos, int count,
	     void (*func) (uintptr_t, int, fattr_t *attr, int, char *),
	     uintptr_t token);

int nfs_write(struct cookie *fh, int offset, int count, void *data,
	      void (*func) (uintptr_t, int, fattr_t *),
	      uintptr_t token);

int nfs_readdir(struct cookie *pfh, int cookie, int size,
		void (*func) (uintptr_t , int, int, struct nfs_filename *, int),
		uintptr_t token);

// Async functions callback based
extern void
nfs_getattr_cb(void * callback, uintptr_t token, struct pbuf *pbuf);
extern void
nfs_lookup_cb(void * callback, uintptr_t token, struct pbuf *pbuf);
extern void
nfs_read_cb(void * callback, uintptr_t token, struct pbuf *pbuf);
extern void
nfs_write_cb(void * callback, uintptr_t token, struct pbuf *pbuf);
extern void
nfs_create_cb(void * callback, uintptr_t token, struct pbuf *pbuf);
extern int
getentries_readdir(struct pbuf *pbuf, int *cookie);
extern void
nfs_readdir_cb(void * callback, uintptr_t token, struct pbuf *pbuf);

/* some error codes from nfsrpc */
#define ERR_OK            0
#define ERR_BAD_MSG      -1
#define ERR_NOT_ACCEPTED -2
#define ERR_FAILURE      -3
#define ERR_NOT_OK       -4
#define ERR_NOT_FOUND    -5
#define ERR_NEXT_AVAIL   -6

#endif /* __NFS_H */
