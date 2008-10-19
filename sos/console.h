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
void console_open(pid_t pid, VNode self, const char *path, fmode_t mode,
		void (*open_done)(pid_t pid, VNode self, fmode_t mode, int status));

/* Close a console file */
void console_close(pid_t pid, VNode self, fildes_t file, fmode_t mode,
		void (*close_done)(pid_t pid, VNode self, fildes_t file, fmode_t mode, int status));

/* Read from a console file */
void console_read(pid_t pid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, void (*read_done)(pid_t pid,
			VNode self, fildes_t file, L4_Word_t pos, char *buf, size_t nbyte, int status));

/* Write to a console file */
void console_write(pid_t pid, VNode self, fildes_t file, L4_Word_t offset,
			const char *buf, size_t nbyte, void (*write_done)(pid_t pid, VNode self,
				fildes_t file, L4_Word_t offset, const char *buf, size_t nbyte, int status));

/* Flush the given console stream to the network */
void console_flush(pid_t pid, VNode self, fildes_t file);

/* List directory entries of a console file (UNSUPPORTED) */
void console_getdirent(pid_t pid, VNode self, int pos, char *name, size_t nbyte);

/* Get file information of a console file */
void console_stat(pid_t pid, VNode self, const char *path, stat_t *buf);

/* Remove a file (UNSUPPORTED) */
void console_remove(pid_t pid, VNode self, const char *path);

#endif // sos/console.h

