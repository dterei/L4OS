#ifndef _CONSOLE_H
#define _CONSOLE_H

#include <sos/sos.h>
#include <serial/serial.h>

#include "vfs.h"

/*
 * Wrappers for console open/read/write operations.
 * See libs/sos/include/sos.h for what they do.
 *
 * Note: Only one console device is currently supported.
 */

// How many consoles we have
#define NUM_CONSOLES 1

// Unlimited Console readers or writers value
#define CONSOLE_RW_UNLIMITED ((unsigned int) (-1))

// struct for storing console read requests (continuation struct)
typedef struct {
	L4_ThreadId_t tid;
	int *rval;
	char *buf;
	size_t nbyte;
	size_t rbyte;
} Console_ReadRequest;

// struct for storing info about a console file
typedef struct {
	// store console device info
	char *path;
	const unsigned int Max_Readers;
	const unsigned int Max_Writers;
	unsigned int readers;
	unsigned int writers;

	// store info about a thread waiting on a read
	// change to a list to support more then one reader
	Console_ReadRequest reader;	
} Console_File;

// The file names of our consoles
extern Console_File Console_Files[];

/*
 * Initialise all console devices adding them to the special file list.
 */
VNode console_init(VNode sf);

void console_open(L4_ThreadId_t tid, VNode self, const char *path, fmode_t mode,
		int *rval, void (*open_done)(L4_ThreadId_t tid, VNode self,
			const char *path, fmode_t mode, int *rval));

void console_close(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
		int *rval, void (*close_done)(L4_ThreadId_t tid, VNode self, fildes_t file, fmode_t mode,
			int *rval));

void console_read(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t pos,
		char *buf, size_t nbyte, int *rval);

void console_write(L4_ThreadId_t tid, VNode self, fildes_t file, L4_Word_t offset,
		const char *buf, size_t nbyte, int *rval);

void console_getdirent(L4_ThreadId_t tid, VNode self, int pos, char *name, size_t nbyte,
		int *rval);

void console_stat(L4_ThreadId_t tid, VNode self, const char *path, stat_t *buf, int *rval);

// callback for read
void serial_read_callback(struct serial *serial, char c);

#endif /* console.h */

