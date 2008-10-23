#include <sos/sos.h>

#include "pipefs.h"

#include "constants.h"
#include "libsos.h"
#include "process.h"
#include "syscall.h"

#define verbose 1

/* Create a new pipe file */
void
pipefs_pipe(pid_t pid, VNode self,
		void (*open_done)(pid_t pid, VNode self, fmode_t mode, int status))
{
	dprintf(1, "*** pipefs_pipe: %d, %p, %p, %d\n", pid, self);
	dprintf(0, "!!! pipefs_pipe: Not implemented for pipe fs\n");
	syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_NOTIMP);
}

/* Open a specified pipe file (NOT SUPPORTED) */
void
pipefs_open(pid_t pid, VNode self, const char *path, fmode_t mode,
		void (*open_done)(pid_t pid, VNode self, fmode_t mode, int status))
{
	dprintf(1, "*** pipefs_open: %d, %p, %p, %d\n", pid, self, path, mode);
	dprintf(0, "!!! pipefs_open: Not implemented for pipe fs\n");
	syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_NOTIMP);
}

/* Read the specified number of bytes from the pipe specified into the buffer buf */
void
pipefs_close(pid_t pid, VNode self, fildes_t file, fmode_t mode,
		void (*close_done) (pid_t pid, VNode self, fildes_t file, fmode_t mode, int status))
{
	dprintf(1, "*** pipefs_close: %d, %p, %d, %d\n", pid, self, file, mode);
	dprintf(0, "!!! pipefs_close: Not implemented for pipe fs\n");
	syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_NOTIMP);
}

/* Close a specified file previously opened with the pipe syscall */
void
pipefs_read(pid_t pid, VNode self, fildes_t file, L4_Word_t pos, char *buf,
		size_t nbyte, void (*read_done)(pid_t pid, VNode self, fildes_t file,
			L4_Word_t pos, char *buf, size_t nbyte, int status))
{
	dprintf(1, "*** pipefs_read: %d, %p, %d, %d, %p, %d\n", pid, self, file, pos, nbyte);
	dprintf(0, "!!! pipefs_read: Not implemented for pipe fs\n");
	syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_NOTIMP);
}

/* Write the specified number of bytes from the buffer buf to a given pipe file */
void
pipefs_write(pid_t pid, VNode self, fildes_t file, L4_Word_t offset, const char *buf,
		size_t nbyte, void (*write_done)(pid_t pid, VNode self, fildes_t file,
			L4_Word_t offset, const char *buf, size_t nbyte, int status))
{
	dprintf(1, "*** pipefs_write: %d, %p, %d, %d, %p, %d\n", pid, self, file, offset, buf, nbyte);
	dprintf(0, "!!! pipefs_write: Not implemented for pipe fs\n");
	syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_NOTIMP);
}

/* Flush the given pipe file to disk. (NOT SUPPORTED) */
void
pipefs_flush(pid_t pid, VNode self, fildes_t file)
{
	dprintf(1, "*** pipefs_flush: %d, %p, %d\n", pid, self, file);
	dprintf(0, "!!! pipefs_flush: Not implemented for pipe fs\n");
	syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_NOTIMP);
}

/* Get directory entries of the pipe filesystem (NOT SUPPORTED) */
void
pipefs_getdirent(pid_t pid, VNode self, int pos, char *name, size_t nbyte)
{
	dprintf(1, "*** pipefs_getdirent: %d, %p, %d, %p, %d\n", pid, self, pos, name, nbyte);
	dprintf(0, "!!! pipefs_getdirent: Not implemented for pipe fs\n");
	syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_NOTIMP);

}

/* Get file details for a specified pipe File (NOT SUPPORTED) */
void
pipefs_stat(pid_t pid, VNode self, const char *path, stat_t *buf)
{
	dprintf(1, "*** pipefs_stat: %d, %p, %p, %p\n", pid, self, path, buf);
	dprintf(0, "!!! pipefs_stat: Not implemented for pipe fs\n");
	syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_NOTIMP);

}

/* Remove a pipe file (NOT SUPPORTED) */
void
pipefs_remove(pid_t pid, VNode self, const char *path)
{
	dprintf(1, "*** pipefs_remove: %d, %p, %p\n", pid, self, path);
	dprintf(0, "!!! pipefs_remove: Not implemented for pipe fs\n");
	syscall_reply(process_get_tid(process_lookup(pid)), SOS_VFS_NOTIMP);
}

