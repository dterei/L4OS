#include <stdio.h>
#include <string.h>
#include <sos/sos.h>

#define FN "access_test"

int main(int argc, char *argv[]) {
	char *die = "hello world";
	char *har = "tricked!";
	fildes_t fd;

	fd = open(FN, FM_WRITE);
	write(fd, har, strlen(har) + 1);
	close(fd);

	printf("%s\n", die);

	fd = open(FN, FM_READ);
	read(fd, die, strlen(die) + 1);
	close(fd);

	printf("%s\n", die);

	die[4] = 'p';
	printf("%s\n", die);

	return 0;
}

