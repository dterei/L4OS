/****************************************************************************
 *
 *      Description: Example startup file for the SOS project.
 *
 *      Author:		    Godfrey van der Linden(gvdl)
 *      Original Author:    Ben Leslie
 *
 ****************************************************************************/


#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

#include <sos/sos.h>

#include "l4.h"
#include "libsos.h"
#include "network.h"

#include "pager.h"
#include "frames.h"
#include "vfs.h"

#define verbose 2

#define ONE_MEG (1 * 1024 * 1024)
#define HEAP_SIZE ONE_MEG /* 1 MB heap */
#define INFINITY (1 << 8)
#define RUN_FRAME_TEST 0

// Set aside some memory for a stack for the
// first user task 
#define STACK_SIZE 0x1000
static L4_Word_t init_stack_s[STACK_SIZE];
static L4_Word_t user_stack_s[STACK_SIZE];

// Init thread - This function starts up our device drivers and runs the first
// user program.
static void
init_thread(void)
{
    L4_KDB_SetThreadName(sos_my_tid(), "init_thread");
    // Initialise the network for libsos_logf_init
    network_init();
	 vfs_init();

    // start executables listed in the the BootInfo
    // hack: crt0 expects to be able to pop 3 words off the stack
    dprintf(0, "user_stack_s start at %lx, end at %lx\n", user_stack_s, &user_stack_s[STACK_SIZE]);
    sos_start_binfo_executables(&user_stack_s[STACK_SIZE-3]);

    // Thread finished - block forever
    for (;;)
        sos_usleep(30 * 1000 * 1000);
}


/* Some IPC labels defined in the L4 documentation */
#define L4_PAGEFAULT	((L4_Word_t) -2)
#define L4_INTERRUPT	((L4_Word_t) -1)
#define L4_EXCEPTION    ((L4_Word_t) -5)

#define TAG_SYSLAB(t)	((short) L4_Label(t) >> 4)

/*
  Syscall loop.

  This implements a very simple, single threaded functions for 
  recieving any IPCs and dispatching to the correct subsystem.

  It currently handles pagefaults, interrupts and special sigma0
  requests.
*/

static __inline__ void 
syscall_loop(void)
{
	dprintf(3, "Entering %s\n", __FUNCTION__);

	L4_Set_NotifyMask(1 << SOS_IRQ_NOTIFY_BIT);

	// Set up which messages we will receive
	L4_Accept(L4_AddAcceptor(L4_UntypedWordsAcceptor,L4_NotifyMsgAcceptor));

	int send = 0;
	L4_Word_t rval;
	L4_Msg_t msg;
	L4_ThreadId_t tid = L4_nilthread;
	L4_Word_t irq_mask = 1 << SOS_IRQ_NOTIFY_BIT;

	for (;;) {
		L4_MsgTag_t tag;

		// Wait for a message, sometimes sending a reply
		if (!send)
			tag = L4_Wait(&tid); // Nothing to send, so we just wait
		else
			tag = L4_ReplyWait(tid, &tid); // Reply to caller and wait for IPC

		if (L4_IpcFailed(tag)) {
			L4_Word_t ec = L4_ErrorCode();
			dprintf(0, "%s: IPC error\n", __FUNCTION__);
			sos_print_error(ec);
			assert( !(ec & 1) );	// Check for recieve error and bail
			send = 0;
			continue;
		}

		// At this point we have, probably, recieved an IPC
		L4_MsgStore(tag, &msg); /* Get the tag */

		if (L4_IsNilThread(tid)) {
			/* async notification */
			L4_Word_t notify_bits = L4_MsgWord(&msg, 0);
			dprintf(2, "%s: async notify, bits=%lx\n", __FUNCTION__, notify_bits);
			send = 0;
			if (notify_bits & irq_mask) {
				/* it's an interrupt */
				int dummy = 0; //we never want to reply here
				dprintf(3, "%s: notification is interrupt\n", __FUNCTION__);
				if (0)
					; // some other interrupt, you need to implement this side
				else
					network_irq(&tid, &dummy);
			}
			continue;
		}

		dprintf(2, "%s: got msg, reply cap %lx (%d), (%d %p)\n", __FUNCTION__,
				tid.raw, L4_ThreadNo(tid), (int) TAG_SYSLAB(tag),
				(void *) L4_MsgWord(&msg, 0));

		//
		// Dispatch IPC according to protocol.
		//
		send = 1; /* In most cases we will want to send a reply */
		switch (TAG_SYSLAB(tag)) {
			case L4_PAGEFAULT:
				// A pagefault occured. Dispatch to the pager
				pager(tid, &msg);
				L4_Set_MsgTag(L4_Niltag);
				break;

			case L4_INTERRUPT:
				/* actually an IRQ lock/unlock message */
				network_irq(&tid, &send);
				break;

			case L4_EXCEPTION:
				dprintf(0, "exception: ip=%lx, sp=%lx\ncpsr=%lx\nexception=%lx, cause=%lx\n\n",
						L4_MsgWord(&msg, 0), L4_MsgWord(&msg, 1), L4_MsgWord(&msg, 2),
						L4_MsgWord(&msg, 3), L4_MsgWord(&msg, 4));
				send = 0;
				break;

			case SOS_NETPRINT:
				network_sendstring_int(msg.tag.X.u, (int*) (msg.msg + 1));
				send = 0;
				break;

			case SOS_DEBUG_FLUSH:
				pager_flush(tid, &msg);	
				send = 0;
				break;

			case SOS_MOREMEM:
				rval = (L4_Word_t) sos_moremem(
						(uintptr_t*) sender2kernel(L4_MsgWord(&msg, 0)),
						(uintptr_t*) sender2kernel(L4_MsgWord(&msg, 1)),
						(unsigned int) L4_MsgWord(&msg, 2));
				*(sender2kernel(L4_MsgWord(&msg, 3))) = rval;
				break;

		// XXX must check that sender2kernel doesn't return null,
		// or the user can crash the kernel!!!
		// XXX check the rights of the page before doing anything
		// in sender2kernel - CAN'T just map with the userspace
		// access rights since then userspace could crash the kernel,
		// and can't just map with all access rights because then
		// user programs could access memory they're not allowed to.
		// XXX what if contiguous virtual pages arent contiguous
		// physically... then like addr[HUUUGE] will actually
		// index in to a completely different physical frame.

			case SOS_OPEN:
				rval = (L4_Word_t) vfs_open(tid,
						(char*) sender2kernel(L4_MsgWord(&msg, 0)),
						(fmode_t) L4_MsgWord(&msg, 1));
				*(sender2kernel(L4_MsgWord(&msg, 2))) = rval;
				//send = 0; // open will reply to cap
				break;

			case SOS_CLOSE:
				rval = (L4_Word_t) vfs_close(tid,
						(fildes_t) L4_MsgWord(&msg, 0));
				*(sender2kernel(L4_MsgWord(&msg, 1))) = rval;
				//send = 0; // open will reply to cap
				break;

			case SOS_READ:
				vfs_read(tid,
						(fildes_t) L4_MsgWord(&msg, 0),
						(char*) sender2kernel(L4_MsgWord(&msg, 1)),
						(size_t) L4_MsgWord(&msg, 2),
						(int*) sender2kernel(L4_MsgWord(&msg, 3)));
				//*(sender2kernel(L4_MsgWord(&msg, 3))) = rval;
				send = 0; // open will reply to cap
				break;

			case SOS_WRITE:
				vfs_write(tid,
						(fildes_t) L4_MsgWord(&msg, 0),
						(char*) sender2kernel(L4_MsgWord(&msg, 1)),
						(size_t) L4_MsgWord(&msg, 2),
						(int*) sender2kernel(L4_MsgWord(&msg, 3)));
				//*(sender2kernel(L4_MsgWord(&msg, 3))) = rval;
				send = 0; // open will reply to cap
				break;

			case SOS_GETDIRENT:
			case SOS_PROCESS_CREATE:
			case SOS_PROCESS_DELETE:
			case SOS_MY_ID:
			case SOS_PROCESS_STATUS:
			case SOS_PROCESS_WAIT:
			case SOS_TIME_STAMP:
			case SOS_SLEEP:
			case SOS_SHARE_VM:
			default:
				// Unknown system call, so we don't want to reply to this thread
				dprintf(0, "!!! unrecognised syscall id=%d\n", TAG_SYSLAB(tag));
				sos_print_l4memory(&msg, L4_UntypedWords(tag) * sizeof(uint32_t));
				send = 0;
				break;
		}
	}
}

//
// Test for M1
//
static void
frame_test(void) {
	static L4_Word_t *page, *page1, *page2;
	static int i;

	dprintf(0, "entered frame_test\n");

	/* Allocate 10 pages and make sure you can touch them all */
	for (i = 0; i < 10; i++) {
		/* Allocate a page */
		page = (L4_Word_t*) frame_alloc();
		assert(page);

		/* Test you can touch the page */
		*page = 0x37;
		assert(*page == 0x37);

		dprintf(0, "(1) Page #%d allocated at %p\n", i, page);
	}

	/* A couple of pages to free later. */
	page1 = (L4_Word_t*) frame_alloc();
	page2 = (L4_Word_t*) frame_alloc();

	/* Test that you eventually run out of memory gracefully. */
	for (;;) {
		page = (L4_Word_t*) frame_alloc();

		if (!page) {
			dprintf(0, "(2) Out of memory!\n");
			break;
		} else {
			*page = 0x37;
			assert(*page == 0x37);
		}
	}

	frame_free((L4_Word_t) page1);
	frame_free((L4_Word_t) page2);

	/* Test that you never run out of memory if you always free frames. */
	for (i = 0; i < INFINITY; i++) {
		page = (L4_Word_t*) frame_alloc();
		assert(page != NULL);

		// Test you can touch the page.
		*page = 0x37;
		assert(*page == 0x37);

		dprintf(0, "(3) Page #%d allocated at %p\n",  i, page);
		frame_free((L4_Word_t) page);
	}
}

//
// Main entry point - called by crt.
//

int
main (void)
{
	// store our thread id
	L4_Set_UserDefinedHandle(L4_rootserver.raw);

	// Initialise initial sos environment
	libsos_init();

	// Find information about available memory
	L4_Word_t low, high;
	sos_find_memory(&low, &high);
	dprintf(0, "Available memory from 0x%08lx to 0x%08lx - %luMB\n", 
			low, high, (high - low) / ONE_MEG);

	// Initialise the various monolithic things
	frame_init((low + HEAP_SIZE), high);
	as_init();

	// Spawn the setup thread which completes the rest of the initialisation,
	// leaving this thread free to act as a pager and interrupt handler.
	(void) sos_thread_new(&init_thread, &init_stack_s[STACK_SIZE]);

	/* Test M1 */
	if (RUN_FRAME_TEST) frame_test();

	dprintf(1, "*** main: about to start syscall loop\n");
	syscall_loop(); // Enter the syscall loop
	/* Not reached */

	return 0;
}

