#include <stdio.h>
#include <string.h>

#include <sos/sos.h>

int main(int argc, char *argv[]) {

	printf("duptest printing to printf\n");

	char *line = "duptest writing to stdout\n";
	write(stdout_fd, line, strlen(line));

	char *line2 = "duptest writing to stderr\n";
	write(stderr_fd, line2, strlen(line2));

	fildes_t newfdout = dup(stdout_fd);

	char *line3 = "duptest dup'd stdout, writing to stdout now\n";
	write(stdout_fd, line3, strlen(line3));

	char *line4 = "duptest dup'd stdout, writing to dup'd fd now\n";
	write(newfdout, line4, strlen(line4));

	return 0;
}

