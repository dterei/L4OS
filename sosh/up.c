#include <sos/sos.h>
#include <stdio.h>

#include "up.h"

int up(int argc, char **argv)
{
	long us = uptime();
	long us2 = us;
	long secs = us / 1000000;
	long mins = secs / 60;

	us2 -= 1000 * secs;
	secs -= 60 * mins;

	printf("up %ld mins %ld secs %lu us (total %lu)\n", mins, secs, us2, us);
	return 0;
}

