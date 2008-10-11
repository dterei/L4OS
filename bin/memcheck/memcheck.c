#include <stdio.h>
#include <sos/sos.h>
#include <l4/types.h>

#define WAIT_FOR (5)

#define verbose 1
#define dprintf(v, buf) if ((v) < verbose) kprint(buf);

int main(int argc, char *argv[]) {
	char buf[64];

	sprintf(buf, ">>> memcheck: there are %d frames in use\n", memuse());
	dprintf(1, buf);

	sprintf(buf, ">>> memcheck: tid of vpager is %ld\n", L4_ThreadNo(vpager()));
	dprintf(1, buf);

	sprintf(buf, ">>> memcheck: waiting for sosh to die\n");
	dprintf(1, buf);
	process_wait(WAIT_FOR);

	sprintf(buf, ">>> memcheck: there are now %d frames in use\n", memuse());
	dprintf(1, buf);

	return 0;
}

