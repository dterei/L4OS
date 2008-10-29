#ifndef _CONSTANTS_H
#define _CONSTANTS_H

/* Include user space relevant globals, redefine them to have a more SOS
 * descriptive name if applicable. Don't include this header in globals.h
 * as then all constants will be exported to user space.
 */
#include <sos/globals.h>

// UDP port serial library uses/sosh.
#define SERIAL_PORT 26706

#define PAGESIZE 4096
#define PAGEALIGN (~((PAGESIZE) - 1))
#define PAGEWORDS ((PAGESIZE) / (sizeof(L4_Word_t)))
#define ONE_MEG (1 * 1024 * 1024)

#define ADDRESS_ALL ((L4_Word_t) (-1))
#define ADDRESS_NONE ((L4_Word_t) (-2))

#define VIRTUAL_PAGER_PRIORITY 250

#define CONSOLE_BUF_SIZ 128
#define COPY_BUFSIZ (PAGESIZE * 4)
#define MAX_ADDRSPACES 256
#define MAX_THREADS 256
#define PROCESS_MAX_FILES 16
#define PROCESS_STDFDS_RESERVE 3
#define PROCESS_MAX_FDS (PROCESS_MAX_FILES + PROCESS_STDFDS_RESERVE)

#define SWAPFILE_FN ".swap"

#endif // constants.h
