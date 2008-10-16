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

#include <sos/sos.h>

#include "alloc.h"
#include "cat.h"
#include "commands.h"
#include "hex.h"
#include "kecho.h"
#include "kill.h"
#include "ls.h"
#include "m5bench.h"
#include "memdump.h"
#include "pid.h"
#include "ps.h"
#include "pt_test.h"
#include "rm.h"
#include "seektest.h"
#include "segfault.h"
#include "sleep.h"
#include "sosh.h"
#include "time.h"
#include "top.h"
#include "up.h"

static stat_t sbuf;
fildes_t in;

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

static int
help(int argc, char *argv[])
{
	printf("Available commands:\n");
	for (int i = 0; sosh_commands[i].command != NULL; i++) {
		printf("\t%s\n", sosh_commands[i].name);
	}
	return 0;
}

struct command sosh_commands[] = {
	{"alloc", alloc},
	{"cat", cat},
	{"dir", ls},
	{"exec", exec},
	{"help", help},
	{"hex", hex},
	{"ls", ls},
	{"kecho", kecho},
	{"kill", kill},
	{"m5bench", m5bench},
	{"memdump", memdump},
	{"pid", pid},
	{"ps", ps},
	{"pt_test", pt_test},
	{"rm", rm},
	{"seektest", seektest},
	{"segfault", segfault},
	{"sleep", sleep},
	{"time", time},
	{"top", top},
	{"up", up},
	{"null", NULL}
};

int
main(int sosh_argc, char *sosh_argv[])
{
	char buf[BUF_SIZ];
	char *argv[MAX_ARGS];
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
			r = read(in, bp, BUF_SIZ-1+buf-bp);
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
				argc = 2;
				argv[1] = argv[0];
				argv[0] = "exec";
				exec(argc, argv);
			}
		}
	}

	close(in);
	printf("[SOS Exiting]\n");
}

