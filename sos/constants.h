#ifndef _CONSTANTS_H
#define _CONSTANTS_H

/* Include user space relevant globals, redefine them to have a more SOS
 * descriptive name if applicable. Don't include this header in globals.h
 * as then all constants will be exported to user space.
 */
#include <sos/globals.h>

/* Random problems seem to occur if all of buffer used, some is needed for
 * nfs header.
 */
#define NFS_HEADER 180
#define NFS_BUFSIZ (IO_MAX_BUFFER - NFS_HEADER)

#define PAGESIZE 4096

#define MAX_ADDRSPACES 256
#define MAX_IO_BUF PAGESIZE
#define MAX_THREADS 1024
#define ONE_MEG (1 * 1024 * 1024)
#define PAGEALIGN (~((PAGESIZE) - 1))
#define PAGEWORDS ((PAGESIZE) / (sizeof(L4_Word_t)))
#define PROCESS_MAX_FILES 16

// swapfile filename
#define SWAPFILE_FN ".swap"

#endif // constants.h
