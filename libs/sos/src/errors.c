#include <sos/errors.h>

char *
sos_error_msg(int error) {
	switch(error) {
		case SOS_VFS_OK: return "Operation Succeeded";
		case SOS_VFS_EOF: return "End of file";
		case SOS_VFS_PERM: return "Invalid Permissions";
		case SOS_VFS_NOFILE: return "Invalid File Descriptor";
		case SOS_VFS_NOVNODE: return "File Doesn't Exist";
		case SOS_VFS_NOMEM: return "Out of Memory";
		case SOS_VFS_NOMORE: return "Can't Open More Files";
		case SOS_VFS_PATHINV: return "Invalid Path";
		case SOS_VFS_CORVNODE: return "Corrupt Vnode";
		case SOS_VFS_NOTIMP: return "Operation Not Supported by this File System";
		case SOS_VFS_WRITEFULL: return "File Writer Slots Full";
		case SOS_VFS_READFULL: return "File Reader Slots Full";
		case SOS_VFS_OPEN: return "File Already Open";
		case SOS_VFS_DIR: return "File is a Directory";
		case SOS_VFS_ERROR:
		default: return "Error Occurred";
	}
}

