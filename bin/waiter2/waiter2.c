#include <stdio.h>
#include <sos/sos.h>

#define WAIT_FOR (6)

int main(int argc, char *argv[]) {
	char buf[64];

	sprintf(buf, ">>> waiter %d waiting for %d\n", my_id(), WAIT_FOR);
	kprint(buf);

	pid_t wokenBy = process_wait(WAIT_FOR);

	sprintf(buf, ">>> waiter %d woken by %d\n", my_id(), wokenBy);
	kprint(buf);

	process_delete(my_id());
	return 0;
}

