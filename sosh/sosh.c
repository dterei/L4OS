/* Simple shell to run on SOS */
/* 
 * Orignally written by Gernot Heiser 
 * - updated by Ben Leslie 2003  
 * - updated by Charles Gray 2006
 */

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sos/sos.h>

#define verbose 0

#define BUF_SIZ 128
#define MAX_ARGS 32
#define MAX_PROCESSES 10


static fildes_t in;
static stat_t   sbuf;

static void 
prstat(const char *name)
{
	/* print out stat buf */

	printf("%c%c%c%c 0x%06x 0x%lx 0x%06lx %s\n",
			sbuf.st_type ==ST_SPECIAL ? 's' : '-',
			sbuf.st_fmode&FM_READ     ? 'r' : '-',
			sbuf.st_fmode&FM_WRITE    ? 'w' : '-',
			sbuf.st_fmode&FM_EXEC     ? 'x' : '-',
			sbuf.st_size, sbuf.st_ctime, sbuf.st_atime, name);
}

static int
cat(int argc, char **argv)
{
	fildes_t fd;
	char buf[BUF_SIZ];
	int num_read, num_written = 0;

	if (argc != 2) {
		printf("Usage: cat filename\n");
		return 1;
	}

	fd = open(argv[1], FM_READ);
	if (fd < 0) {
		printf("%s cannot be opened\n", argv[1]);
		return 1;
	}

	printf("<%s>\n", argv[1]);

	while( (num_read = read(fd, buf, BUF_SIZ)) > 0 ) {
		buf[num_read] = '\0';
		printf("%s", buf);
		//num_written = write(stdout_fd, buf, num_read);
	}

	if( num_read == -1)
	{
		printf( "error on read\n" );
		return 1;
	}

	if( num_written == -1)
	{
		printf( "error on write\n" );
		return 1;
	}

	return 0;
}


static int
cp(int argc, char **argv)
{
	fildes_t fd, fd_out;
	char *file1, *file2;
	char buf[BUF_SIZ];
	int num_read, num_written = 0;

	if (argc != 3) {
		printf("Usage: cp from to %d\n", argc);
		return 1;
	}

	file1 = argv[1];
	file2 = argv[2];

	fd = open(file1, FM_READ);
	fd_out = open(file2, FM_WRITE);

	if (fd < 0) {
		printf("%s cannot be opened\n", file1);
		return 1;
	}

	if (fd_out < 0) {
		printf("%s cannot be opened\n", file2);
		return 1;
	}

	while( (num_read = read( fd, buf, BUF_SIZ) ) > 0 )
		num_written = write(fd_out, buf, num_read);

	if( num_read == -1 || num_written == -1 )
	{
		printf( "error on cp\n" );
		return 1;
	}

	return 0;
}

static int 
ps(int argc, char **argv)
{

	process_t *process;
	int i, processes;

	printf("ps: calling malloc\n");
	process = malloc( MAX_PROCESSES * sizeof(*process) );
	printf("ps: malloc done\n");

	if( process == NULL )
	{
		printf( "%s: out of memory\n", argv[0] );
		return 1;
	}

	processes = process_status(process, MAX_PROCESSES);

	printf ("TID SIZE   STIME   CTIME COMMAND\n");

	for (i=0; i<processes; i++) {
		printf ("%3x %4x %7d %7d %s\n", process[i].pid, process[i].size,
				process[i].stime, process[i].ctime, process[i].command);
	}

	free( process );

	return 0;
}

static int
exec(int argc, char **argv)
{
	pid_t pid;
	int r;
	int bg = 0;

	if (argc < 2 || (argc > 2 && argv[2][0] != '&')) {
		printf("Usage: exec filename [&]\n");
		return 1;
	}

	if ( (argc > 2) && (argv[2][0] == '&') ) {
		bg = 1;
	}

	if (bg == 0) {
		r = close(in);
		assert(r == 0);
	}

	pid = process_create(argv[1]);
	if (pid>=0) {
		printf("Child pid=%d\n", pid);
		if (bg == 0) {
			process_wait(pid);
		}
	} else {
		printf("Failed!\n");
	}
	if (bg == 0) {
		in = open("console", FM_READ);
		assert(in>=0);
	}
	return 0;
}

static int
dir(int argc, char **argv)
{
	int  i=0, r;
	char buf[BUF_SIZ];

	if( argc > 2 )
	{
		printf( "usage: %s [file]\n", argv[0] );
		return 1;
	}

	if (argc == 2)
	{
		r = stat(argv[1], &sbuf);
		if( r < 0 )
		{
			printf("stat(%s) failed: %d\n", argv[1], r);
			return 0;
		}
		prstat(argv[1]);
		return 0;
	}

	while (1) {
		r = getdirent(i, buf, BUF_SIZ);
		if (r<0) {
			printf("dirent(%d) failed: %d\n", i, r);
			break;
		} else if (!r) {
			break;
		}
#if 0
		printf("dirent(%d): \"%s\"\n", i, buf);
#endif
		r = stat(buf, &sbuf);
		if (r<0) {
			printf("stat(%s) failed: %d\n", buf, r);
			break;
		}
		prstat(buf);
		i++;
	}
	return 0;
}

static int
segfault(int argc, char **argv) {
	int *null = NULL;
	return *null;
}

static int 
nap(int argc, char **argv) {
	if (argc < 2) {
		printf("usage: %s msec\n", argv[0]);
		return 1;
	}

	int msec = atoi(argv[1]);
	sleep(msec);
	return 0;
}

static int 
howlong(int argc, char **argv) {
	long us = uptime();
	long us2 = us;
	long secs = us / 1000000;
	long mins = secs / 60;

	us2 -= 1000 * secs;
	secs -= 60 * mins;

	printf("up %ld mins %ld secs %lu us (total %lu)\n", mins, secs, us2, us);
	return 0;
}

struct command {
	char *name;
	int (*command)(int argc, char **argv);
};

static int help(int argc, char *argv[]);
static int time(int argc, char *argv[]);

struct command commands[] = {
	{"dir", dir},
	{"ls", dir},
	{"cat", cat},
	{"cp", cp},
	{"ps", ps},
	{"exec", exec},
	{"segfault", segfault},
	{"sleep", nap},
	{"uptime", howlong},
	{"help", help},
	{"time", time},
	{"null", NULL}
};

static int
help(int argc, char *argv[]) {
	printf("Available commands:\n");
	for (int i = 0; commands[i].command != NULL; i++) {
		printf("\t%s\n", commands[i].name);
	}
	return 0;
}

static int
time(int argc, char **argv) {
	long int start = 0, finish = 0;

	if (argc < 2) {
		printf("Usage: time cmd [args]\n");
		return 1;
	}

	for (int i = 0; commands[i].name != NULL; i++) {
		if (strcmp(commands[i].name, argv[1]) == 0) {
			start = uptime();
			commands[i].command(argc - 1, argv + 1);
			finish = uptime();
			printf("*******\n%ld us\n", finish - start);
			return 0;
		}
	}

	printf("time: command %s not found\n", argv[1]);
	return 1;
}

int
main(void)
{
	char             buf[BUF_SIZ];
	char             *argv[MAX_ARGS];
	int              i, r, done, found, new, argc;
	char             *bp, *p;

	sosuser_init();

	in = open("console", FM_READ);
	assert (in >= 0);

	bp   = buf;
	done = 0;
	new  = 1;

	printf("\n[SOS Starting]\n");

	while (!done) {
		if (new) {
			printf("$ ");
		}
		new   = 0;
		found = 0;

		while (!found && !done) {
			r = read(in, bp, BUF_SIZ-1+buf-bp);
			if (r<0) {
				printf("Console read failed!\n");
				done=1;
				break;
			}
			bp[r] = 0;		/* terminate */
			if (verbose > 1) {
				printf("sosh: just read %s, %d", bp, r);
				if (bp[r-1] != '\n') {
					printf("\n");
				}
			}
			for (p=bp; p<bp+r; p++) {
				if (*p == '\03') {	/* ^C */
					printf("^C\n");
					p   = buf;
					new = 1;
					break;
				} else if (*p == '\04') {   /* ^D */
					p++;
					found = 1;
				} else if (*p == '\010' || *p == 127) {	    
					/* ^H and BS and DEL */
					if (p>buf) {
						printf("\010 \010");
						p--;
						r--;
					}
					p--;
					r--;
				} else if (*p == '\n') {    /* ^J */
					if (verbose > 0) {
						printf("%c",*p);
					}
					*p    = 0;
					found = p>buf;
					p     = buf;
					new   = 1;
					break;
				} else if (verbose > 0)  {
					printf("%c",*p);
				}
			}
			bp = p;
			if (bp == buf) {
				break;
			}
		}

		if (!found) {
			continue;
		}

		argc = 0;
		p = buf;

		while (*p != '\0')
		{
			/* Remove any leading spaces */
			while (*p == ' ')
				p++;
			if (*p == '\0')
				break;
			argv[argc++] = p; /* Start of the arg */
			while (*p != ' ' && *p != '\0') {
				p++;
			}

			if (*p == '\0')
				break;

			/* Null out first space */
			*p = '\0';
			p++;
		}

		if (argc == 0) {
			continue;
		}

		found = 0;

		for (i = 0; i < sizeof(commands) / sizeof(struct command); i++) {
			if (strcmp(argv[0], commands[i].name) == 0) {
				commands[i].command(argc, argv);
				found = 1;
				break;
			}
		}

		/* Didn't find a command */
		if (found == 0) {
			/* They might try to exec a program */
			if (stat(argv[0], &sbuf) != 0) {
				printf("Command \"%s\" not found\n", argv[0]);
			} else if (!(sbuf.st_fmode & FM_EXEC)) {
				printf("File \"%s\" not executable\n", argv[0]);
			} else {
				/* Execute the program */
				argc = 2;
				argv[1] = argv[0];
				argv[0] = "exec";
				exec(argc, argv);
			}
		}
	}
	printf("[SOS Exiting]\n");
}

