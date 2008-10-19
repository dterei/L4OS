/* defines some debug functions which can be used by user space */
#ifndef _LIB_SOS_DEBUG_H_
#define _LIB_SOS_DEBUG_H_

void debug_printf(const char *format, ...);
void debug_print_L4Err(L4_Word_t ec);

#endif
