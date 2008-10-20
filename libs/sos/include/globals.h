/* Defenition of Constants used in SOS which are relevant to User Space */

#ifndef _LIB_SOS_GLOBALS_H
#define _LIB_SOS_GLOBALS_H

#ifndef TRUE
#define TRUE       1L 
#endif

#if TRUE != 1
#error TRUE is not defined to 1
#endif

#ifndef FALSE
#define FALSE      0L 
#endif

#if FALSE != 0
#error FALSE is not defined to 0
#endif

#define NFS_HEADER 180
#define NFS_BUFSIZ 1280

/* Max buffer size for write and read */
#define IO_MAX_BUFFER (NFS_BUFSIZ - NFS_HEADER)

#endif // libs/sos/globals.h
