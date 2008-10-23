#ifndef _CONSTANTS_H
#define _CONSTANTS_H

/* Include user space relevant globals, redefine them to have a more SOS
 * descriptive name if applicable. Don't include this header in globals.h
 * as then all constants will be exported to user space.
 */
#include <sos/globals.h>

// UDP port serial library uses/sosh.
#define SERIAL_PORT (26706)

// Console buffer size
#define CONSOLE_BUF_SIZ 128

/* Random problems seem to occur if all of buffer used, some is needed for
 * nfs header.
 */
#define PAGESIZE 4096

#define ADDRESS_ALL ((L4_Word_t) (-1))
#define ADDRESS_NONE ((L4_Word_t) (-2))
#define MAX_ADDRSPACES 256
#define COPY_BUFSIZ PAGESIZE
#define MAX_THREADS 1024
#define ONE_MEG (1 * 1024 * 1024)
#define PAGEALIGN (~((PAGESIZE) - 1))
#define PAGEWORDS ((PAGESIZE) / (sizeof(L4_Word_t)))
#define PROCESS_MAX_FILES 16

// swapfile filename
#define SWAPFILE_FN ".swap"

#endif // constants.h
