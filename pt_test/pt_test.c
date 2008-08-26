#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <sos/pager.h>
#include <sos/sos.h>
#include <sos/thread.h>

#include <l4/types.h>
#include <l4/ipc.h>
#include <l4/message.h>
#include <l4/thread.h>

#define NPAGES 128

static void
do_pt_test( char *buf )
{
	int i;
	//L4_Fpage_t fp;

	/* set */
	for(i = 0; i < NPAGES; i += 4)
		buf[i * 1024] = i;

	/* flush */
	sos_debug_flush();

	/* check */
	for(i = 0; i < NPAGES; i += 4)
		assert(buf[i*1024] == i);
}

static void
pt_test(void)
{
	/* need a decent sized stack */
	char buf1[NPAGES * 1024], *buf2 = NULL;

	/* check the stack is above phys mem */
	assert((void *) buf1 > (void *) 0x2000000);

	/* stack test */
	do_pt_test(buf1);

	/* heap test */
	buf2 = malloc(NPAGES * 1024);
	assert(buf2);
	do_pt_test(buf2);
	free(buf2);
}

int
main(int argc, char *argv[])
{
	printf("Starting pt_test...\n");
	pt_test();
	printf("Finished pt_test.  Did it work?\n");
	thread_block();
	return 0;
}

