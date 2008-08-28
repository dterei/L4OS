#ifndef _CONSOLE_H
#define _CONSOLE_H

#include <sos/sos.h>

#include "vfs.h"

/*
 * Wrappers for console open/read/write operations.
 * See libs/sos/include/sos.h for what they do.
 */

VNode console_init(void);
fildes_t console_open(const char *path, fmode_t mode);
int console_close(fildes_t file);
int console_read(fildes_t file, char *buf, size_t nbyte);
int console_write(fildes_t file, const char *buf, size_t nbyte);

#endif /* console.h */
