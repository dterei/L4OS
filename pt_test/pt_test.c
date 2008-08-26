#include <stdlib.h>
#include <assert.h>

// TODO: xxx HACK needed for printf. Need to move printf to libc
#include "../tty_test/ttyout.c"

#define NPAGES 128
#define SOS_DEBUG_FLUSH 1

static void
sos_debug_flush( void )
{
	//TODO: Should be a system call, so live in libc.
	L4_Msg_t msg;
	L4_MsgClear(&msg);
	L4_Set_MsgLabel(&msg, SOS_DEBUG_FLUSH);
	L4_MsgLoad(&msg);
	L4_Send(L4_rootserver);
}

/* called from pt_test */
static void
do_pt_test( char *buf )
{
	int i;
	//L4_Fpage_t fp;

	/* set */
	for(i = 0; i < NPAGES; i += 4)
		buf[i * 1024] = i;

	/* flush */
	assert(!"make a syscall to flush the user address space");
	sos_debug_flush();

	/* check */
	for(i = 0; i < NPAGES; i += 4)
		assert(buf[i*1024] == i);
}

static void
pt_test( void )
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
main( int argc, char *argv[])
{
	pt_test();
}
