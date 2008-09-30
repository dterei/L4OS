#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sos/sos.h>

#include "m5bench.h"
#include "sosh.h"
#include "time.h"

#define CREATEFILES_DEFAULT 10

#define TIMER_SLEEP_DEFAULT 1000000

#define IO_KBYTES_DEFAULT 4
#define IO_BLOCK_DEFAULT 1024

#define DIR_LOOPS_DEFAULT 10

#define IO_FILENAME ".m5bench_ioband"
#define SEEK_FILENAME ".m5bench_seek";

static void m5test_help(int argc, char *argv[]);
static void m5test_timer(int argc, char *argv[]);
static void m5test_createfiles(int argc, char *argv[]);
static void m5test_iobandwidth(int argc, char *argv[]);
static void m5test_getdirent(int argc, char *argv[]);
static void m5test_lseek(int argc, char *argv[]);
static void m5test_benchmark(int argc, char *argv[]);

struct command {
	char *name;
	void (*command)(int argc, char *argv[]);
};

static
struct command m5commands[] = {
	{"timer", m5test_timer},
	{"createfiles", m5test_createfiles},
	{"ioband", m5test_iobandwidth},
	{"getdirent", m5test_getdirent},
	{"seek", m5test_lseek},
	{"benchmark", m5test_benchmark},
	{"help", m5test_help},
	{"NULL", NULL}
};

int
m5bench(int argc, char **argv)
{
	if (argc < 2)
	{
		m5test_help();
		return 0;
	}

	int found = 0;
	for (int i = 0; i < sizeof(m5commands) / sizeof(struct command); i++) {
		if (strcmp(argv[1], m5commands[i].name) == 0) {
			m5commands[i].command(argc, argv);
			return 0;
		}
	}

	m5test_help();
	return 0;
}

static
void
m5test_help(int argc, char *argv[])
{
	printf("Usage: m5bench [test]\n");
	printf("\nTests: ");
	for (int i = 0; m5commands[i].command != NULL; i++) {
		printf(" %s", m5commands[i].name);
	}
	printf("\n");
}

static
void
m5test_benchmark(int argc, char *argv[])
{
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
}

static
void
m5test_timer(int argc, char *argv[])
{
	long time = TIMER_SLEEP_DEFAULT;

	if (argc == 3)
	{ time = atoi(argv[2]); }

	if (argc > 3 || time <= 0)
	{
		printf("Usage: m5bench timer [#microseconds]\n");
		return;
	}

	printf("M5 Test: timer started\n");
	printf("sleeping for %d microseconds\n", time);
	long time = uptime();

	usleep(time);

	time = uptime() - time;
	printf("Slept for %ld microseconds\n", time);
	printf("M5 Test: timer Finished (took %ld microseconds)\n", time);
}

static
void
m5test_createfiles(int argc, char *argv[])
{
	//XXX The following code crashes SOS
	//char *filename = "a0";
	//filename[1] = '0' + i;
	
	char filename[14];
	fildes_t fp;
	int files = CREATEFILES_DEFAULT;
	
	if (argc == 3)
	{ files = atoi(argv[2]);

	if (argc > 3 || files <= 0)
	{
		printf("Usage: m5bench createsfiles [#files]\n");
		return;
	}

	printf("M5 Test: createfiles started (files %d)\n", files);

	long time = uptime();
	for (int i = 0; i < files; i++)
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

		if (fremove(filename) < 0)
		{
			printf("%s can't be removed!", filename);
			printf("!!! M5 Benchmark createfiles FAILED\n");
			continue;
		}
		else
		{
			printf("file removed: %s\n", filename);
		}
	}

	time = uptime() - time;
	printf("M5 Test: createfiles Finished (took %ld microseconds)\n", time);
}

static
void
m5test_iobandwidth(int argc, char *argv[])
{
	int kb = IO_KBYTES_DEFAULT;
	int bsize = IO_BLOCK_DEFAULT:

	if (argc == 3)
	{ kb = atoi(argv[2]); }
	if (argc == 4)
	{ bsize = atoi(argv[3]); }

	if (argc > 4 || kb <= 0 || bsize <= 0)
	{
		printf("Usage: m5bench ioband [#kbytes] [#buffer size (bytes)]\n");
		return;
	}

	printf("M5 Test: iobandwidth started (kbytes %d)\n", kb);

	/* 1 kb data block to writer out */
	char *data = malloc(sizeof(char) * bsize);

	fildes_t fp = open(IO_FILENAME, FM_WRITE);
	long time = uptime();

	int num_writ = 0;
	for (int i = 0; i < kb * 1024; i += bsize)
	{
		int d = write(fp, data, bsize);
		num_writ += d;
		if (d < 0)
		{
			printf("test failed!\n");
			return;
		}
	}

	
	long dt = uptime();
	printf("took %ld to write %d bytes (%d kb) to file.\n", dt - time, num_writ,
			(int) ((((float) num_writ) /1024) + 0.5) );

	close(fp);
	fp = open(IO_FILENAME, FM_READ);

	printf("reading %d kbytes from file.\n", kb);

	int c, num_read = 0;
	while( (c = read( fp, data, bsize) ) > 0 )
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
	printf("took %ld to read %d bytes (%d kb) from a file.\n", dt, num_read, num_read/1024);

	time = uptime() - time;
	close(fp);

	if (fremove(IO_FILENAME) < 0)
	{
		printf("%s can't be removed!", IO_FILENAME);
		printf("!!! M5 Benchmark ioband FAILED\n");
		continue;
	}
	else
	{
		printf("file removed: %s\n", filename);
	}

	printf("M5 Test: iobandwidth Finished (took %ld microseconds)\n", time);
}

static
void
m5test_getdirent(int argc, char *argv[])
{
	int loops = DIR_LOOPS_DEFAULT;

	if (argc > 3)
	{
		printf("Usage: m5bench getdirent [#times]\n");
		return;
	}

	if (argc == 3)
	{ loops = argv[2]; }

	printf("M5 Test: getdirent started (loops %d)\n", loops);

	long time = uptime();
	char buf[128];

	for (int i = 0; i < loops; i++)
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
m5test_lseek(int argc, char *argv[])
{
	printf("M5 Test: seek started\n");

	fildes_t fp = open(SEEK_FILENAME, FM_WRITE);
	long time = uptime();

	int d = write(fp, "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX", 32);
	printf("Wrote %d bytes to file (%s)\n", d, SEEK_FILENAME);

	/* SEEK_SET */
	int pos = 0;
	d = lseek(fp, pos, SEEK_SET);
	printf("Seek'd to pos (%d), status (%d) using SEEK_SET\n", pos, d);
	d = write(fp, "AAAA", 4);
	printf("Wrote %d bytes to file (%s)\n", d, SEEK_FILENAME);

	/* SEEK_CUR */
	int pos = 10;
	d = lseek(fp, pos, SEEK_CUR);
	printf("Seek'd to pos (%d), status (%d) using SEEK_SET\n", pos, d);
	d = write(fp, "AAAA", 4);
	printf("Wrote %d bytes to file (%s)\n", d, SEEK_FILENAME);

	/* SEEK_END */
	int pos = 4;
	d = lseek(fp, pos, SEEK_END);
	printf("Seek'd to pos (%d), status (%d) using SEEK_END\n", pos, d);
	d = write(fp, "AAAA", 4);
	printf("Wrote %d bytes to file (%s)\n", d, SEEK_FILENAME);

	printf("M5 Test: seek finished (took %ld microseconds)\n", time);
	close(fp);
}

