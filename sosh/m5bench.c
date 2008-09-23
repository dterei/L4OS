#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sos/sos.h>

#include "m5bench.h"
#include "sosh.h"
#include "time.h"

#define CREATE_LIMIT 10
#define TIMER_SLEEP 1000
#define IO_KBYTES 4
#define DIR_LOOPS 4

static void m5test_help(void);
static void m5test_timer(void);
static void m5test_createfiles(void);
static void m5test_iobandwidth(void);
static void m5test_iocopy(void);
static void m5test_writeread(void);
static void m5test_getdirent(void);
static void m5test_stat(void);

struct command {
	char *name;
	void (*command)(void);
};

static
struct command commands[] = {
	{"timer", m5test_timer},
	{"createfiles", m5test_createfiles},
	{"ioband", m5test_iobandwidth},
	{"iocopy", m5test_iocopy},
	{"writeread", m5test_writeread},
	{"getdirent", m5test_getdirent},
	{"stat", m5test_stat},
	{"help", m5test_help}
};

static int benchmark(int argc, char *argv[]) {
	char *timeArgs[4];
	printf("*** TESTING READ PERFORMANCE\n");

	timeArgs[0] = "time";
	timeArgs[1] = "cat";

	printf("\n");
	timeArgs[2] = "1kb";
	time(3, timeArgs);
	printf("\n");
	timeArgs[2] = "2kb";
	time(3, timeArgs);
	printf("\n");
	timeArgs[2] = "4kb";
	time(3, timeArgs);
	printf("\n");
	timeArgs[2] = "8kb";
	time(3, timeArgs);
	printf("\n");
	timeArgs[2] = "16kb";
	time(3, timeArgs);
	printf("\n");
	timeArgs[2] = "32kb";
	time(3, timeArgs);

	printf("*** TESTING READ/WRITE PERFORMANCE\n");

	timeArgs[1] = "cp";

	printf("\n");
	timeArgs[2] = "1kb";
	timeArgs[3] = "1kb.cp";
	time(4, timeArgs);
	printf("\n");
	timeArgs[2] = "2kb";
	timeArgs[3] = "2kb.cp";
	time(4, timeArgs);
	printf("\n");
	timeArgs[2] = "4kb";
	timeArgs[3] = "4kb.cp";
	time(4, timeArgs);
	printf("\n");
	timeArgs[2] = "8kb";
	timeArgs[3] = "8kb.cp";
	time(4, timeArgs);
	printf("\n");
	timeArgs[2] = "16kb";
	timeArgs[3] = "16kb.cp";
	time(4, timeArgs);
	printf("\n");
	timeArgs[2] = "32kb";
	timeArgs[3] = "32kb.cp";
	time(4, timeArgs);

	return 0;
}

int
m5bench(int argc, char **argv)
{
	if (argc < 2)
	{
		m5test_help();
		return 0;
	}

	int found = 0;
	for (int i = 0; i < sizeof(commands) / sizeof(struct command); i++) {
		if (strcmp(argv[1], commands[i].name) == 0) {
			commands[i].command();
			found = 1;
			break;
		}
	}

	if (found == 0)
	{
		m5test_help();
	}

	(void) benchmark;

	return 0;
}

static
void
m5test_help(void)
{
	printf("Usage: m5bench [test]\n");
	printf("\nTests: ");
	for (int i = 0; commands[i].command != NULL; i++) {
		printf(" %s", commands[i].name);
	}
	printf("\n");
}

static
void
m5test_timer(void)
{
	printf("M5 Test: timer started\n");
	printf("sleeping for %d milliseconds\n", TIMER_SLEEP);
	long time = uptime();

	usleep(TIMER_SLEEP);

	time = uptime() - time;
	printf("Slept for %ld milliseconds\n", time/1000);
	printf("M5 Test: timer Finished (took %ld microseconds)\n", time);
}

static
void
m5test_createfiles(void)
{
	//XXX The following code crashes SOS
	//char *filename = "a0";
	//filename[1] = '0' + i;
	
	char filename[14];
	fildes_t fp;

	printf("M5 Test: createfiles started (files %d)\n", CREATE_LIMIT);

	long time = uptime();
	for (int i = 0; i < CREATE_LIMIT; i++)
	{
		sprintf(filename, "%s%d", "m5test_files_", i);
		fp = open(filename, FM_WRITE);
		if (fp < 0)
		{
			printf("%s can't be opened!", filename);
			printf("!!! M5 Benchmark createfiles FAILED\n");
			continue;
		}
		else
		{
			printf("file opened: %s\n", filename);
		}

		if (close(fp) < 0)
		{
			printf("%s can't be closed!", filename);
			printf("!!! M5 Benchmark createfiles FAILED\n");
			continue;
		}
		else
		{
			printf("file closed: %s\n", filename);
		}
	}

	time = uptime() - time;
	printf("M5 Test: createfiles Finished (took %ld microseconds)\n", time);
}

static
void
m5test_iobandwidth(void)
{
	printf("M5 Test: iobandwidth started (kbytes %d)\n", IO_KBYTES);

	printf("writing %d kbytes to file.\n", IO_KBYTES);

	fildes_t fp = open("m5bench_ioband", FM_WRITE);
	long time = uptime();

	int num_writ = 0;
	for (int i = 0; i < IO_KBYTES*32; i++)
	{
		int d = write(fp, "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX", 32);
		num_writ += d;
		if (d < 0)
		{
			printf("test failed!\n");
			return;
		}
	}

	
	long dt = uptime();
	printf("took %ld to write %d kbytes to file.\n", dt - time, num_writ/1024);

	close(fp);
	fp = open("m5bench_ioband", FM_READ);

	printf("reading %d kbytes from file.\n", IO_KBYTES);

	int c, num_read = 0;
	char buf[1024];
	while( (c = read( fp, buf, 1024) ) > 0 )
	{
		if (c > 0)
		{
			num_read += c;
		}
	}

	if( c < 0 )
	{
		printf( "error on read\n" );
	}

	dt = uptime() - dt;
	printf("took %ld to read %d kbytes from a file.\n", dt, num_read/1024);

	time = uptime() - time;
	close(fp);
	printf("M5 Test: iobandwidth Finished (took %ld microseconds)\n", time);
}

static
void
m5test_iocopy(void)
{

}

static
void
m5test_writeread(void)
{

}

static
void
m5test_getdirent(void)
{
	printf("M5 Test: getdirent started (loops %d)\n", DIR_LOOPS);

	long time = uptime();
	char buf[128];

	for (int i = 0; i < DIR_LOOPS; i++)
	{
		int j = 0;
		while (1)
		{
			int r = getdirent(j, buf, 128);
			j++;
			if (r<0)
			{
				printf("dirent(%d) failed: %d\n", j, r);
				break;
			}
			else if (!r)
			{
				break;
			}
		}
	}

	time = uptime() - time;
	printf("M5 Test: getdirent Finished (took %ld microseconds)\n", time);

}

static
void
m5test_stat(void)
{

}

