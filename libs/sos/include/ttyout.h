#ifndef _LIB_TTYOUT_H
#define _LIB_TTYOUT_H

#include <stdio.h>

void ttyout_init(void);
size_t sos_write(const void *data, long int position, size_t count, void *handle);
size_t sos_read(void *data, long int position, size_t count, void *handle);

#endif
