#include <stdio.h>

#include <sos/sos.h>

#include "l4.h"
#include "thread.h"

void thread_kill(L4_ThreadId_t tid) {
	L4_Word_t result = L4_ThreadControl(tid, L4_nilspace,
			L4_nilthread, L4_nilthread, L4_nilthread, 0, NULL);

	if (result == 0) {
		printf("!!! thread_kill(%d) failed\n", (int) L4_ThreadNo(tid));
	}
}
