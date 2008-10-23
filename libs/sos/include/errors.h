/* Defines some error codes used by sos */

#ifndef _LIB_SOS_ERRORS_H
#define _LIB_SOS_ERRORS_H

/* VFS Return Codes */
typedef enum {
	SOS_VFS_OK = 0,
	SOS_VFS_EOF = -1,
	SOS_VFS_ERROR = -999, // make sure not to add more then 999 return codes :)
	SOS_VFS_PERM,
	SOS_VFS_NOFILE,
	SOS_VFS_NOVNODE,
	SOS_VFS_NOMEM,
	SOS_VFS_NOMORE,
	SOS_VFS_PATHINV,
	SOS_VFS_CORVNODE,
	SOS_VFS_NOTIMP,
	SOS_VFS_WRITEFULL,
	SOS_VFS_READFULL,
	SOS_VFS_OPEN,
	SOS_VFS_DIR,
	SOS_VFS_EXIST,
	SOS_VFS_BADMODE,
} vfs_return_t;

char *sos_error_msg(int error);

#endif
