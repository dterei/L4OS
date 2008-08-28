#ifndef _CONSOLE_H
#define _CONSOLE_H

#include <serial/serial.h>

#include <sos/sos.h>

#include "vfs.h"

/*
 * Wrappers for console open/read/write operations.
 * See libs/sos/include/sos.h for what they do.
 */

// How many consoles we have
#define NUM_CONSOLES 1

// Unlimited Console readers or writers value
#define CONSOLE_RW_UNLIMITED ((unsigned int) (-1))

// struct for storing info about a console file
typedef struct {
	char *path;
	const unsigned int Max_Readers;
	const unsigned int Max_Writers;
	unsigned int readers;
	unsigned int writers;
	// struct serial *serial;
	L4_ThreadId_t reader_tid; // should be a list really but simple for moment
} Console_File;

// The file names of our consoles
extern Console_File Console_Files[];

/*
 * Initialise all console devices adding them to the special file list.
 */
SpecialFile console_init(SpecialFile sf);

fildes_t console_open(L4_ThreadId_t tid, VNode self, const char *path,
		fmode_t mode);

int console_close(L4_ThreadId_t tid, VNode self, fildes_t file);

int console_read(L4_ThreadId_t tid, VNode self, fildes_t file,
		char *buf, size_t nbyte);

int console_write(L4_ThreadId_t tid, VNode self, fildes_t file,
		const char *buf, size_t nbyte);

// callback for read
void serial_read_callback(struct serial *serial, char c);

#endif /* console.h */

