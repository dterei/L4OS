#ifndef _CONSTANTS_H_
#define _CONSTANTS_H_

#define PAGESIZE 4096

#define MAX_ADDRSPACES 256
#define MAX_IO_BUF PAGESIZE
#define MAX_THREADS 1024
#define ONE_MEG (1 * 1024 * 1024)
#define PAGEALIGN (~((PAGESIZE) - 1))
#define PAGEWORDS ((PAGESIZE) / (sizeof(L4_Word_t)))
#define PROCESS_MAX_FILES 16

#endif // constants.h
