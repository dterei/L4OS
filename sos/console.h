#ifndef _CONSOLE_H
#define _CONSOLE_H

#include <sos/sos.h>
#include <serial/serial.h>

#include "vfs.h"

/*
 * Wrappers for console open/read/write operations.
 * See libs/sos/include/sos.h for what they do.
 *
 * Note: Only one console device is currently supported.
 */

/*
 * Initialise all console devices adding them to the special file list.
 */
VNode console_init(VNode sf);

/* Open a console file */
void console_open(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		void (*open_done)(L4_ThreadId_t tid, VNode self, fmode_t mode, int status));

/* Close a console file */
void console_close(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
		void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode, int status));

/* Read from a console file */
void console_read(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, void (*read_done)(L4_ThreadId_t tid,
			VNode self, fildes_t file, L4_Word_t pos, char *buf, size_t nbyte, int status));

/* Write to a console file */
void console_write(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
			const char *buf, size_t nbyte, void (*write_done)(L4_ThreadId_t tid, VNode self,
				fildes_t file, L4_Word_t offset, const char *buf, size_t nbyte, int status));

/* Flush the given console stream to the network */
void console_flush(L4_ThreadId_t tid, VNode self, fildes_t file);

/* List directory entries of a console file (UNSUPPORTED) */
void console_getdirent(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte);

/* Get file information of a console file */
void console_stat(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf);

/* Remove a file (UNSUPPORTED) */
void console_remove(L4_ThreadId_t tid, VNode self, const char *path);

#endif // sos/console.h

