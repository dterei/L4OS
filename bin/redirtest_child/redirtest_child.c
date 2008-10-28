#include <stdio.h>
#include <string.h>

#include <sos/sos.h>

int main(int argc, char *argv[]) {

	kprint("^^^ REDIRTEST_CHILD START\n");
	printf("redir child, I just talk to stdout\n");
	char *line = "redir_child, I just talk to stderr\n";
	write(stderr_fd, line, strlen(line));
	kprint("^^^ REDIRTEST_CHILD END\n");

	return 0;
}

