/* Simple shell to run on SOS */

/* 
 * Orignally written by Gernot Heiser 
 * - updated by Ben Leslie 2003  
 * - updated by Charles Gray 2006
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include <sos/sos.h>
#include <sos/globals.h>

#define verbose 0

static stat_t sbuf;
static fildes_t in;

static int help(int argc, char **argv);

static void
exitFailure(char *msg) {
	int id = my_id();
	printf("sosh (%d) %s!\n", id, msg);
	printf("sosh (%d) exiting\n", id);
	exit(EXIT_FAILURE);
}

static int
exec(int argc, char **argv) {
	pid_t pid;
	int r;
	int bg = 0;

	if (argc < 2 || (argc > 2 && argv[2][0] != '&')) {
		printf("Usage: exec filename [&]\n");
		return 1;
	}

	if ((argc > 2) && (argv[2][0] == '&')) {
		bg = 1;
	}

	if (bg == 0) {
		r = close(in);
		if (r != 0) {
			exitFailure("can't close console\n");
		}
	}

	pid = process_create(argv[1]);

	if (pid >= 0) {
		if (bg == 0) {
			process_wait(pid);
		}
	} else {
		printf("Failed!\n");
	}

	if (bg == 0) {
		in = open("console", FM_READ);
		if (in < 0) {
			exitFailure("can't open console for reading");
		}
	}

	return 0;
}

struct command {
	char *name;
	int (*command)(int argc, char **argv);
};

struct arg {
	char *arg;
	int set;
};

#define LINE_LEN 80
#define ARG_COUNT 4

#define ARG_A 0
#define ARG_L 1

static
struct arg args[] = {
	{"a", 0},
	{"l", 0},
};

static void prstat(const char *name) {
	// array corresponding to file types.
	static char type[] = {'s', '-', 'd'};
	// make sure we don't go out of array
	int t = sbuf.st_type > 2 ? 0 : sbuf.st_type;

	time_t ntime = sbuf.st2_ctime / 1000;
	char c_stime[26];
	strftime(c_stime, 26, "%F %T", localtime(&ntime));

	ntime = sbuf.st2_atime / 1000;
	char m_stime[26];
	strftime(m_stime, 26, "%F %T", localtime(&ntime));

	printf("%c%c%c%c %10u %s %s %s\n",
			type[t],
			sbuf.st_fmode & FM_READ     ? 'r' : '-',
			sbuf.st_fmode & FM_WRITE    ? 'w' : '-',
			sbuf.st_fmode & FM_EXEC     ? 'x' : '-',
			sbuf.st_size, c_stime, m_stime, name);
}

static int cat(int argc, char *argv[]) {
	fildes_t fd;
	char buf[IO_MAX_BUFFER];
	int num_read, num_written = 0, read_tot = 0, write_tot = 0;
	int ret = 0;

	if (argc != 2) {
		printf("Usage: cat filename\n");
		return 1;
	}

	fd = open(argv[1], FM_READ);
	if (fd < 0) {
		printf("%s cannot be opened\n", argv[1]);
		return 1;
	}

	//printf("<%s>\n", argv[1]);

	while ((num_read = read(fd, buf, IO_MAX_BUFFER - 1)) > 0 ) {
		buf[num_read] = '\0';
		// use write instead of printf as printf has some bugs with
		// printing weird chars
		if ((num_written = write(stdout_fd, buf, num_read)) < 0) {
			break;
		}
		read_tot += num_read;
		write_tot += num_written;
	}

	close(fd);

	if (num_read < 0) {
		printf("cat failed: error on read (%d)\n", num_read);
		printf("Can't read file: %s\n", sos_error_msg(num_read));
		kprint("error on read\n" );
		ret = 1;
	}

	if (num_written < 0) {
		printf("cat failed: error on print (%d)\n", num_written);
		printf("Can't print file: %s\n", sos_error_msg(num_written));
		kprint("error on write\n" );
		ret = 1;
	}

	if (verbose > 0) {
		sprintf(buf, "Total Read: %d, Total Write: %d\n", read_tot, write_tot);
		printf("%s", buf);
		kprint(buf);
	}

	return ret;
}

static int cp(int argc, char **argv) {
	fildes_t fd, fd_out;
	char *file1, *file2;
	char buf[IO_MAX_BUFFER];
	int num_read, num_written = 0;

	if (argc != 3) {
		printf("Usage: cp from to %d\n", argc);
		return 1;
	}

	file1 = argv[1];
	file2 = argv[2];

	fd = open(file1, FM_READ);
	if (fd < 0) {
		printf("%s cannot be opened: %s\n", file1, sos_error_msg(fd));
		return 1;
	}

	fd_out = open(file2, FM_WRITE);
	if (fd_out < 0) {
		printf("%s cannot be opened: %s\n", file2, sos_error_msg(fd));
		close(fd);
		return 1;
	}

	while ((num_read = read( fd, buf, IO_MAX_BUFFER) ) > 0) {
		num_written = write(fd_out, buf, num_read);
	}

	if (num_read == -1 || num_written == -1) {
		close(fd);
		close(fd_out);
		printf("error on cp: %s\n", sos_error_msg(fd));
		return 1;
	}

	close(fd);
	close(fd_out);
	return 0;
}

static int ls(int argc, char **argv) {
	int i, r;
	char buf[IO_MAX_BUFFER];

	if (argc > ARG_COUNT) {
		printf("usage: %s [-a] [-l] [file]\n", argv[0]);
		return 1;
	}

	//reset the args
	for (int j = 0; j < sizeof(args) / sizeof(struct arg); j++) {
		args[j].set = 0;
	}

	// Pass args
	for (int j = 1; j < argc; j++) {
		if (strncmp(argv[j], "-", 1) == 0) {
			for (int j2 = 1; j2 < strlen(argv[j]); j2++) {
				for (int j3 = 0; j3 < sizeof(args) / sizeof(struct arg); j3++) {
					if (strncmp(&argv[j][j2], args[j3].arg, 1) == 0) {
						args[j3].set = 1;
					}
				}
			}
		}
	}

	if (argc > 1 && (strncmp(argv[argc - 1], "-", 1) != 0)) {
		r = stat(argv[argc - 1], &sbuf);

		if (r < 0) {
			printf("stat(%s) failed: %s\n", argv[argc - 1], sos_error_msg(r));
			return 0;
		}

		prstat(argv[argc - 1]);
		return 0;
	}

	int linec = 0;
	for (i = 0;; i++) {
		r = getdirent(i, buf, IO_MAX_BUFFER);

		if (r == SOS_VFS_EOF) {
			break;
		} else if (r < 0) {
			printf("dirent(%d) failed: %s\n", i, sos_error_msg(r));
			break;
		}

		if (args[ARG_A].set == 1 || strncmp(buf, ".", 1) != 0) {
			if (args[ARG_L].set == 1) {
				r = stat(buf, &sbuf);
				if (r < 0) {
					printf("stat(%s) failed: %s\n", buf, sos_error_msg(r));
					break;
				}

				prstat(buf);
			} else {
				if (linec + strlen(buf) > LINE_LEN) {
					printf("\n");
					linec = 0;
				}
				linec += printf("%s ", buf);
			}
		} else {
			continue;
		}
	}

	if (args[ARG_L].set != 1) {
		printf("\n");
	}

	return 0;
}
static int kill(int argc, char **argv) {
	if (argc < 2) {
		printf("usage: %s pid\n", argv[0]);
	} else if (process_delete(atoi(argv[1])) == (-1)) {
		printf("%s %s failed (invalid process)\n", argv[0], argv[1]);
	} else {
		printf("successfully killed process %s\n", argv[1]);
	}

	return 0;
}

static int alloc(int argc, char **argv) {
	int written;
	int size = (4096 - 32);
	char *buf = (char*) malloc(size);

	printf("---> memory for %s allocated at %p (%u)\n",
			argv[1], buf, (unsigned int) buf);

	printf("---> memory for %s physically at %p\n",
			argv[1], (void*) memloc((L4_Word_t) buf));

	printf("---> stack (%p) is at %p\n",
			&buf, (void*) memloc((L4_Word_t) &buf));

	written = 0;

	for (int i = 1; i < argc; i++) {
		strcpy(buf + written, argv[i]);
		written += strlen(argv[i]);

		if (i < argc - 1) {
			buf[written] = ' ';
			written++;
		}
	}

	for (int i = written; i < size; i++) {
		buf[i] = 0;
	}

	return 0;
}

static int pid(int argc, char **argv) {
	printf("%u\n", my_id());
	return 0;
}

static int memdump(int argc, char **argv) {
	const int SIZE = 4096;
	const int BLOCK = 512;

	if (argc < 3) {
		printf("usage: %s address file\n", argv[0]);
		return 1;
	}

	printf("Start memdump\n");

	char *dump = (char*) (atoi(argv[1]) & ~(SIZE - 1));

	if (dump == NULL) {
		printf("must give in decimal form\n");
		return 1;
	} else {
		printf("dumping %p\n", dump);
	}

	fildes_t out = open(argv[2], FM_WRITE);

	// touch it first
	int nWritten = write(out, dump, BLOCK);
	printf("physically at %p\n", (void*) memloc((L4_Word_t) dump));

	while (nWritten < SIZE) {
		nWritten += write(out, dump + nWritten, BLOCK);
		printf("memdump: writen %d bytes\n", nWritten);
	}

	close(out);

	return 0;
}

static int segfault(int argc, char **argv) {
	int *null = NULL;
	return *null;
}


typedef int test_t;

static int soshtest(int argc, char **argv) {
	int i, j, passed;
	test_t **buf;
	const int NUM_SLOTS = 128;
	const int SLOTSIZE = 192;
	const int STACKSIZE = 4096;
	test_t stack[STACKSIZE];

	// Test heap

	printf("allocating heap\n");

	buf = (test_t**) malloc(NUM_SLOTS * sizeof(test_t*));

	for (i = 0; i < NUM_SLOTS; i++) {
		buf[i] = (test_t*) malloc(SLOTSIZE * sizeof(test_t));

		if (i % 16 == 0) {
			printf("%d... ", i / 16);
			flush(stdout_fd);
		}
	}

	printf("\nfilling heap with first test values\n");

	for (i = 0; i < NUM_SLOTS; i++) {
		if (i % 16 == 0) {
			printf("%d... ", i / 16);
			flush(stdout_fd);
		}

		for (j = 0; j < SLOTSIZE; j++) {
			buf[i][j] = i*i + j;
		}
	}

	printf("\ntesting heap\n");

	// Do in two rounds in case memory is going crazy

	for (i = 0; i < NUM_SLOTS; i++) {
		passed = 1;

		if (i % 16 == 0) {
			printf("%d... ", i / 16);
			flush(stdout_fd);
		}

		for (j = 0; j < SLOTSIZE; j++) {
			if (buf[i][j] != i*i + j) {
				printf("pt_test: failed for i=%d j=%d (%d vs %d)\n",
						i, j, buf[i][i], i*i + j);
				passed = 0;
			}
		}
	}

	// Try a different touch value

	printf("\ntesting for a second time\n");

	for (i = 0; i < NUM_SLOTS; i++) {
		for (j = 0; j < SLOTSIZE; j++) {
			buf[i][j] = (i - 1) * (i - 2) * (j + 1);
		}

		if (i % 16 == 0) {
			printf("%d... ", i / 16);
			flush(stdout_fd);
		}
	}

	// And again

	printf("\nverifing\n");

	for (i = 0; i < NUM_SLOTS; i++) {
		passed = 1;

		if (i % 16 == 0) {
			printf("%d... ", i / 16);
			flush(stdout_fd);
		}

		for (j = 0; j < SLOTSIZE; j++) {
			if (buf[i][j] != (i - 1) * (i - 2) * (j + 1)) {
				printf("pt_test: failed for i=%d j=%d (%d vs %d)\n",
						i, j, buf[i][i], (i - 1) * (i - 2) * (j + 1));
				passed = 0;
			}
		}
	}


	// Test stack

	printf("\ntesting stack\n");

	for (i = 0; i < STACKSIZE; i++) {
		stack[i] = i*i - i;
	}

	for (i = 0; i < STACKSIZE; i++) {
		if (stack[i] != i*i - i) {
			printf("pt_test: failed for i=%d\n", i);
		}
	}

	printf("\ndone\n");

	return 0;
}

/*
static int time(int argc, char **argv) {
	uint64_t start = 0, finish = 0;

	if (argc < 2) {
		printf("Usage: time cmd [args]\n");
		return 1;
	}

	for (int i = 0; sosh_commands[i].name != NULL; i++) {
		if (strcmp(sosh_commands[i].name, argv[1]) == 0) {
			start = uptime();
			sosh_commands[i].command(argc - 1, argv + 1);
			finish = uptime();
			printf("*******\n%llu us\n", finish - start);
			return 0;
		}
	}

	printf("time: command %s not found\n", argv[1]);
	return 1;
}
*/

struct command sosh_commands[] = {
	{"alloc", alloc},
	{"cat", cat},
	{"cp", cp},
	{"exec", exec},
	{"help", help},
	{"kill", kill},
	{"ls", ls},
	{"memdump", memdump},
	{"pid", pid},
	{"segfault", segfault},
	{"soshtest", soshtest},
	//{"time", time},
	{"null", NULL}
};

static int help(int argc, char **argv)
{
	printf("Available commands:\n");
	for (int i = 0; sosh_commands[i].command != NULL; i++) {
		printf("\t%s\n", sosh_commands[i].name);
	}
	return 0;
}

int
main(int sosh_argc, char *sosh_argv[])
{
	char buf[IO_MAX_BUFFER];
	char *argv[32];
	int i, r, done, found, new, argc;
	char *bp, *p = "";

	in = open("console", FM_READ);
	if (in < 0) {
		exitFailure("can't open console for reading");
	}

	bp  = buf;
	new = 1;
	done = 0;

	printf("\n[SOS Starting]\n");

	while (!done) {
		if (new) {
			printf("$ ");
		}
		new   = 0;
		found = 0;

		while (!found && !done) {
			r = read(in, bp, IO_MAX_BUFFER-1+buf-bp);
			if (r<0) {
				printf("Console read failed!\n");
				done=1;
				break;
			}
			bp[r] = '\0';		/* terminate */
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

		if (verbose > 1) {
			printf("Command (pre space filter): %s\n", p);
		}

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

		if (verbose > 0) {
			printf("Command (post space filter): %s\n", argv[0]);
		}

		found = 0;

		for (i = 0; i < sizeof(sosh_commands) / sizeof(struct command); i++) {
			if (strcmp(argv[0], sosh_commands[i].name) == 0) {
				sosh_commands[i].command(argc, argv);
				found = 1;
				break;
			}
		}

		/* Didn't find a command */
		if (found == 0) {
			if (verbose > 1) {
				printf("try to execute program: %s\n", argv[0]);
			}
			/* They might try to exec a program */
			if (stat(argv[0], &sbuf) != 0) {
				printf("Command \"%s\" not found\n", argv[0]);
			} else if (!(sbuf.st_fmode & FM_EXEC)) {
				printf("File \"%s\" not executable\n", argv[0]);
			//} else if (sbuf.st_type == ST_DIR) {
			//	printf("File \"%s\" is a directory\n", argv[0]);
			} else {
				if (verbose > 1) {
					printf("Type: %d, Mode: %d\n", sbuf.st_type, sbuf.st_fmode);
				}
				/* Execute the program */
				if (argc > 31) {
					printf("Command line has too many arguments!\n");
				} else {
					// shift args one up.
					for (int i = argc - 1; i >= 0; i--) {
						argv[i + 1] = argv[i];
					}
					argv[0] = "exec";
					exec(argc + 1, argv);
				}
			}
		}
	}

	close(in);
	printf("[SOS Exiting]\n");
}

