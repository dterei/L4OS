#include <stdio.h>
#include <sos/sos.h>

#define PROGRAM "forkbomb2"

int main(int argc, char *argv[]) {
	int id = my_id();
	char buf[8];

	sprintf(buf, "+%d\n", id);
	kprint(buf);

	process_create(PROGRAM);
	process_create(PROGRAM);

	sprintf(buf, "-%d\n", id);
	kprint(buf);

	return 0;
}

