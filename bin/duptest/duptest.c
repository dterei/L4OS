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
	printf("dupp'd stdout: %d = %d\n", stdout_fd, newfdout);

	char *line3 = "duptest dup'd stdout, writing to stdout now\n";
	write(stdout_fd, line3, strlen(line3));

	char *line4 = "duptest dup'd stdout, writing to dup'd fd now\n";
	write(newfdout, line4, strlen(line4));

	fildes_t fd = open(".duptest", FM_WRITE);
	fildes_t fd2 = dup(fd);

	char *line5 = "line1: written using orig fd\n";
	char *line6 = "line2: written using new fd\n";
	write(fd, line5, strlen(line5));
	write(fd2, line6, strlen(line6));
	close(fd);

	char *line7 = "line3: written using new fd (just closed of fd)\n";
	write(fd2, line7, strlen(line7));
	close(fd2);

	return 0;
}

