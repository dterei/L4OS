#include <stdio.h>
#include <string.h>

#include <sos/sos.h>

int main(int argc, char *argv[]) {

	printf("redir starting new process which will write to file .redirtest\n");

	fildes_t fd = open(".redirtest", FM_WRITE);
	process_create2("redirtest_child", fd, VFS_NIL_FILE, VFS_NIL_FILE);

	close(fd);

	return 0;
}

