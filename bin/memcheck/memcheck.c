#include <stdio.h>
#include <sos/sos.h>
#include <l4/types.h>

#define WAIT_FOR (5)

int main(int argc, char *argv[]) {
	char buf[64];

	sprintf(buf, ">>> memcheck: there are %d frames in use\n", memuse());
	kprint(buf);

	sprintf(buf, ">>> memcheck: tid of vpager is %ld\n", L4_ThreadNo(vpager()));
	kprint(buf);

	sprintf(buf, ">>> memcheck: waiting for sosh to die\n");
	kprint(buf);
	process_wait(WAIT_FOR);

	sprintf(buf, ">>> memcheck: there are now %d frames in use\n", memuse());
	kprint(buf);

	process_delete(my_id());
	return 0;
}

