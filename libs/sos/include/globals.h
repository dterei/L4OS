/* Defenition of Constants used in SOS which are relevant to User Space */

#ifndef _LIB_SOS_GLOBALS_H
#define _LIB_SOS_GLOBALS_H

#define FALSE 0
#define TRUE 1

#define NFS_HEADER 180
#define NFS_BUFSIZ 1280

/* Max buffer size for write and read */
#define IO_MAX_BUFFER (NFS_BUFSIZ - NFS_HEADER)

#endif // libs/sos/globals.h
