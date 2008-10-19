#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <l4/ipc.h>

#include <sos/debug.h>

#define DBUF_SIZE 128

void
debug_printf(const char *format, ...)
{
	char s[DBUF_SIZE];
	int ret;
	va_list va;

	va_start(va, format);
	ret = vsnprintf(s, DBUF_SIZE, format, va);
	va_end(va);

	for (int i = 0; i < strnlen(s, DBUF_SIZE); i++) {
		L4_KDB_PrintChar(s[i]);
	}
}

void
debug_print_L4Err(L4_Word_t ec) {
	L4_Word_t e, p;

	e = (ec >> 1) & 15;
	p = ec & 1;

	debug_printf("IPC %s error, code is 0x%lx (see p125)\n", p ? "receive" : "send", e);
}

