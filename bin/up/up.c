#include <sos/sos.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
	uint64_t us = uptime();
	uint64_t us2 = us;

	uint64_t secs = us / 1000000;
	us2 -= 1000 * secs;

	uint64_t mins = secs / 60;
	secs -= 60 * mins;

	uint64_t hours = mins / 60;
	mins -= 60 * hours;

	uint64_t days = hours / 24;
	hours -= 24 * days;

	printf("up %llu days %llu hours %llu mins %llu secs %llu us (total %llu)\n", days, hours, mins, secs, us2, us);
	return 0;
}

