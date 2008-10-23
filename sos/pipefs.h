#ifndef _PIPEFS_H
#define _PIPEFS_H

#include "vfs.h"

/*
 * Pipe File System Implementation.
 *
 * Pipes are a simple IPC method commonly provided by UNIX
 * based and other OS's.
 *
 * Works by creating backing stores in memory. Each 'file'
 * is of page size.
 *
 * Pipe files have to be created with the syscall 'pipe(void)'
 * which will return two fds, one for reading one for writing.
 * They can then otherwise be used with standard vfs syscalls.
 *
 * Pipes are unidirectional and blocking. A program writing
 * to a full pipe will block until another program reads
 * from the pipe and vice versa.
 */

/* Create a new pipe, returns two fds, one for writing one for reading */
void pipefs_pipe(pid_t pid, VNode self,
		void (*open_done)(pid_t pid, VNode self, fmode_t mode, int status));

/* Open a specified pipe file (NOT SUPPORTED) */
void pipefs_open(pid_t pid, VNode self, const char *path, fmode_t mode,
		void (*open_done)(pid_t pid, VNode self, fmode_t mode, int status));

/* Read the specified number of bytes from the pipe specified into the buffer buf */
void pipefs_close(pid_t pid, VNode self, fildes_t file, fmode_t mode,
		void (*close_done)(pid_t pid, VNode self, fildes_t file,
			fmode_t mode, int status));

/* Close a specified file previously opened with the pipe syscall */
void pipefs_read(pid_t pid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, void (*read_done)(pid_t pid,
			VNode self, fildes_t file, L4_Word_t pos, char *buf, size_t nbyte, int status));

/* Write the specified number of bytes from the buffer buf to a given pipe file */
void pipefs_write(pid_t pid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, void (*write_done)(pid_t pid, VNode self,
			fildes_t file, L4_Word_t offset, const char *buf, size_t nbyte, int status));

/* Flush the given pipe file to disk. (NOT SUPPORTED) */
void pipefs_flush(pid_t pid, VNode self, fildes_t file);

/* Get directory entries of the pipe filesystem (NOT SUPPORTED) */
void pipefs_getdirent(pid_t pid, VNode self, int pos, char *name, size_t nbyte);

/* Get file details for a specified pipe File (NOT SUPPORTED) */
void pipefs_stat(pid_t pid, VNode self, const char *path, stat_t *buf);

/* Remove a pipe file (NOT SUPPORTED) */
void pipefs_remove(pid_t pid, VNode self, const char *path);

#endif
