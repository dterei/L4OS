#include <sos/sos.h>
#include <stdio.h>

#include "up.h"

int up(int argc, char **argv)
{
	uint64_t us = uptime();
	uint64_t us2 = us;
	uint64_t secs = us / 1000000;
	uint64_t mins = secs / 60;

	us2 -= 1000 * secs;
	secs -= 60 * mins;

	printf("up %llu mins %llu secs %llu us (total %llu)\n", mins, secs, us2, us);
	return 0;
}

