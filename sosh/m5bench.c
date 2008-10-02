#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sos/sos.h>
#include <sos/globals.h>

#include "m5bench.h"
#include "sosh.h"
#include "time.h"

#define CREATEFILES_DEFAULT 10

#define TIMER_SLEEP_DEFAULT 1000000

#define IO_KBYTES_DEFAULT 4
#define IO_BLOCK_DEFAULT 1024

#define DIR_LOOPS_DEFAULT 10

#define IO_FILENAME ".m5bench_ioband"
#define SEEK_FILENAME ".m5bench_seek"

static void m5test_help(int argc, char *argv[]);
static void m5test_timer(int argc, char *argv[]);
static void m5test_createfiles(int argc, char *argv[]);
static void m5test_iobandwidth(int argc, char *argv[]);
static void m5test_getdirent(int argc, char *argv[]);
static void m5test_lseek(int argc, char *argv[]);
static void m5test_benchmark(int argc, char *argv[]);
static void m5test_trycrash(int argc, char*argv[]);
static void m5test_console_read(int argc, char *argv[]);
static void m5test_console_write(int argc, char *argv[]);

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
	{"trycrash", m5test_trycrash},
	{"consoleread", m5test_console_read},
	{"consolewrite", m5test_console_write},
	{"help", m5test_help},
	{"NULL", NULL},
};

int
m5bench(int argc, char **argv)
{
	if (argc < 2)
	{
		m5test_help(argc, argv);
		return 0;
	}

	for (int i = 0; i < sizeof(m5commands) / sizeof(struct command); i++)
	{
		if (strcmp(argv[1], m5commands[i].name) == 0)
		{
			m5commands[i].command(argc, argv);
			return 0;
		}
	}

	m5test_help(argc, argv);
	return 0;
}

static
void
m5test_help(int argc, char *argv[])
{
	printf("Usage: m5bench [test]\n");
	printf("\nTests: ");
	for (int i = 0; m5commands[i].command != NULL; i++)
	{
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
	long sleep= TIMER_SLEEP_DEFAULT;

	if (argc >= 3)
	{ sleep = atoi(argv[2]); }

	if (argc > 3 || time <= 0)
	{
		printf("Usage: m5bench timer [#microseconds]\n");
		return;
	}

	printf("M5 Test: timer started\n");
	printf("sleeping for %ld microseconds\n", sleep);
	long time = uptime();

	usleep(sleep);

	time = uptime() - time;
	printf("Slept for %ld microseconds\n", time);
	printf("M5 Test: timer Finished (took %ld microseconds)\n", time);
}

static
void
m5test_trycrash(int argc, char*argv[])
{
	printf("Try to crash SOS\n");
	printf("This should potentially crash sosh, but SOS should be fine\n");
	printf("Add any know bugs to this test to check for future regressions\n");

	printf("Test 1: Try to change memory in read only text section\n");
	//XXX The following code crashes SOS, should just crash sosh
	int i = 1;
	char *filename = "a0";
	filename[1] = '0' + i;
	printf("Test 1: PASSED\n");

	printf("trycrash PASSED\n");
}

static
void
m5test_createfiles(int argc, char *argv[])
{
	char filename[14];
	fildes_t fp;
	int files = CREATEFILES_DEFAULT;
	
	if (argc >= 3)
	{ files = atoi(argv[2]); }

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
	int bsize = IO_BLOCK_DEFAULT;
	int loops = 1;

	if (argc >= 3)
	{ kb = atoi(argv[2]); }
	if (argc >= 4)
	{ bsize = atoi(argv[3]); }
	if (argc >= 5)
	{ loops = atoi(argv[4]); }

	if (argc > 5 || kb <= 0 || bsize <= 0 || loops <= 0)
	{
		printf("Usage: m5bench ioband [#kbytes] [#buffer size (bytes)] [#times]\n");
		return;
	}

	if (bsize > IO_MAX_BUFFER)
	{
		printf("Specified buffer too big, use one less than  or equal to %d bytes\n", IO_MAX_BUFFER);
		printf("Continue anyway [n]: ");
		char c = 'n';
		if (read(in, &c, 1) <= 0 || (c != 'y' && c != 'Y')) {
			return;
		}
	}

	for (int lp = 0; lp < loops; lp++)
	{
		printf("M5 Test: iobandwidth started (kbytes %d) (buffer %d) (loop %d)\n", kb, bsize, lp);

		/* data block to writer out */
		char *data = malloc(sizeof(char) * bsize);
		for (int i = 0; i < bsize; i++)
		{
			data[i] = (char) i;
		}

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

		long dtw = uptime() - time;
		printf("took %ld to write %d bytes (%d kb) to file.\n", dtw, num_writ, num_writ/1024);

		/* Slightly convulouted way of calucalating kb/s as no fp on xscale */
		long t2 = dtw / 10000;
		if (t2 != 0) {
			long speed = num_writ / t2;
			speed *= 100;
			speed /= 1024;
			printf("Speed: %ld kb/s\n", speed);
		}

		close(fp);
		fp = open(IO_FILENAME, FM_READ);

		printf("reading %d kbytes from file.\n", kb);

		time = uptime();
		int c, num_read = 0;
		while( (c = read( fp, data, bsize) ) > 0 )
		{
			if (c > 0)
			{
				num_read += c;
			}
			else
			{
				printf( "error on read\n" );
			}
		}

		long dtr = uptime() - time;
		printf("took %ld to read %d bytes (%d kb) from a file.\n", dtr, num_read, num_read/1024);

		/* Slightly convulouted way of calucalating kb/s as no fp on xscale */
		t2 = dtr / 10000;
		if (t2 != 0) {
			long speed = num_read / t2;
			speed *= 100;
			speed /= 1024;
			printf("Speed: %ld kb/s\n", speed);
		}

		close(fp);

		if (fremove(IO_FILENAME) < 0)
		{
			printf("%s can't be removed!", IO_FILENAME);
			printf("!!! M5 Benchmark ioband FAILED\n");
		}
		else
		{
			printf("file removed: %s\n", IO_FILENAME);
		}

		printf("checking integrity of data\n");

		int data_ok = 1;
		for (int i = 0; i < bsize; i++)
		{
			if (data[i] != (char) i)
			{
				printf("Error: %d : %c != %c\n", i, data[i], (char) i);
				data_ok = 0;
			}
		}

		free(data);

		printf("M5 Test: iobandwidth Finished (took %ld microseconds)\n", dtw + dtr);

		if (data_ok)
		{
			printf("Data OK\n");
		}
		else
		{
			printf("Data CORRUPT\n");
		}

		printf("M5 Test: iobandwidth Finished (took %ld microseconds)\n", time);
	}
}

static
void
m5test_getdirent(int argc, char *argv[])
{
	int loops = DIR_LOOPS_DEFAULT;

	if (argc >= 3)
	{ loops = atoi(argv[2]); }

	if (argc > 3 || loops <= 0)
	{
		printf("Usage: m5bench getdirent [#times]\n");
		return;
	}

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
	pos = 10;
	d = lseek(fp, pos, SEEK_CUR);
	printf("Seek'd to pos (%d), status (%d) using SEEK_SET\n", pos, d);
	d = write(fp, "AAAA", 4);
	printf("Wrote %d bytes to file (%s)\n", d, SEEK_FILENAME);

	/* SEEK_END */
	pos = 7;
	d = lseek(fp, pos, SEEK_END);
	printf("Seek'd to pos (%d), status (%d) using SEEK_END\n", pos, d);
	d = write(fp, "AAAA", 4);
	printf("Wrote %d bytes to file (%s)\n", d, SEEK_FILENAME);

	time = uptime() - time;
	close(fp);

	printf("M5 Test: seek finished (took %ld microseconds)\n", time);
}

static
void
m5test_console_read(int argc, char *argv[])
{
	char buf[BUF_SIZ];

	printf("M5 Test: console read\n");
	printf("Enter a read in buffer size [4]: ");

	int bs = 4;
	int r;
	if ((r = read(in, buf, BUF_SIZ)) > 0 && buf[0] != '\n') {
		buf[r] = '\0';
		bs = atoi(buf);
		if (bs <= 0) {
			printf("Invalid buffer size!\n");
			return;
		}
	}

	char *buf2 = malloc(sizeof(char) * bs);

	printf("Enter a string to read into buffer of size %d: ", bs);
	r = read(in, buf2, bs);
	printf("Read %d bytes\n", r);
	if (r > 0)
	{
		printf("Entered string: /");
		/* Use loop so as to avoid having to null terminate string.
		 * The supplied printf doesnt seem to support specifiying
		 * the string width so cant do it that way.
		 */
		for (int i = 0; i < r; i++) {
			printf("%c", buf2[i]);
		}
		printf("/\n");
	}

	free(buf2);
}

static
void
m5test_console_write(int argc, char *argv[])
{
	printf("M5 Test: console write\n");
	int bsize = 0, wsize = 0;
	if (argc != 4 || (bsize = atoi(argv[2])) <= 0 || (wsize = atoi(argv[3])) <= 0)
	{
		printf("Usage: m5bench consolewrite [#buffer] [#towrite]\n");
		return;
	}

	char *buf = malloc(sizeof(char) * bsize);
	for (int i = 0; i < bsize; i++)
	{
		buf[i] = 'A';
	}

	printf("Write %d length buffer as %d length buffer\n", bsize, wsize);

	int r = write(stdout_fd, buf, wsize);
	printf("\nWrote %d bytes\n", r);

	free(buf);
}

