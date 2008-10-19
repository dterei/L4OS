#ifndef _SOS_IPC_H
#define _SOS_IPC_H

#include <stdarg.h>

#include <l4/types.h>
#include <l4/message.h>
#include <l4/ipc.h>


/* Provides a nicer interface to IPC that avoids having to deal with
 * L4 specifics like MsgRegisters and loading words. */
#define NO_REPLY 0 // == IPC_SEND
#define YES_REPLY 1 // == IPC_CALL

#define SOS_IPC_CALL 2
#define SOS_IPC_SEND 3
#define SOS_IPC_REPLY 4

/* Base send function, can take multiple words to send and accepts an
 * array of return values to fill from the reply ipc msg.
 */
int ipc_send(L4_ThreadId_t tid, L4_Word_t label, int reply, int nRval,
		L4_Word_t *rvals, int msgLen, ...);

int ipc_send_v(L4_ThreadId_t tid, L4_Word_t label, int reply,
		int nRval, L4_Word_t *rvals, int msgLen, va_list va);

/* Sends an ipc accepting multiple msg words but only returning on return
 * value.
 */
L4_Word_t ipc_send_simple(L4_ThreadId_t tid, L4_Word_t label, int reply,
		int msgLen, ...);

/* The following bellow are derived from ipc_send_simple but are provided
 * to insure type saftey as a ipc_send or ipc_send_simple can be tricky
 * to use and maintain as the msgLen paramater must be kept in line
 * with the vararg number of msg words.
 *
 * The functions bellow provide some low but common numbers.
 */

/* Send an ipc with just a label and no msg words and only returning
 * one return value.
 */
L4_Word_t ipc_send_simple_0(L4_ThreadId_t tid, L4_Word_t label, int reply);

/* Sends an ipc returning one return value and containing one msg word. */
L4_Word_t ipc_send_simple_1(L4_ThreadId_t tid, L4_Word_t label, int reply,
		L4_Word_t w1);

/* Sends an ipc returning one return value and containing two msg word. */
L4_Word_t ipc_send_simple_2(L4_ThreadId_t tid, L4_Word_t label, int reply,
		L4_Word_t w1, L4_Word_t w2);

/* Sends an ipc returning one return value and containing three msg word. */
L4_Word_t ipc_send_simple_3(L4_ThreadId_t tid, L4_Word_t label, int reply,
		L4_Word_t w1, L4_Word_t w2, L4_Word_t w3);

/* Sends an ipc returning one return value and containing four msg word. */
L4_Word_t ipc_send_simple_4(L4_ThreadId_t tid, L4_Word_t label, int reply,
		L4_Word_t w1, L4_Word_t w2, L4_Word_t w3, L4_Word_t w4);

#endif
