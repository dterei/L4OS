#include <stdio.h>
#include <sos/sos.h>

int main(int argc, char *argv[]) {
	int id = my_id();
	char buf[32];

	sprintf(buf, "+%d\n", id);
	kprint(buf);

	sprintf(buf, ".%d-%x\n", id, ((int) uptime()) & 0x0000ffff);
	fildes_t fd = open(buf, FM_WRITE);

	process_create2("pt_test", fd, VFS_NIL_FILE, VFS_NIL_FILE);
	process_create2("elfswap_test", fd, VFS_NIL_FILE, VFS_NIL_FILE);
	process_create2("stackbomb2", fd, VFS_NIL_FILE, VFS_NIL_FILE);

	process_create("nuke");
	process_create("nuke");

	sprintf(buf, "-%d\n", id);
	kprint(buf);

	close(fd);

	return 0;
}

