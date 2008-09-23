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

#include "cat.h"
#include "commands.h"
#include "cp.h"
#include "exec.h"
#include "kecho.h"
#include "ls.h"
#include "m5bench.h"
#include "pid.h"
#include "ps.h"
#include "pt_test.h"
#include "rm.h"
#include "segfault.h"
#include "sleep.h"
#include "sosh.h"
#include "time.h"
#include "up.h"

static int
help(int argc, char *argv[])
{
	printf("Available commands:\n");
	for (int i = 0; sosh_commands[i].command != NULL; i++) {
		printf("\t%s\n", sosh_commands[i].name);
	}
	return 0;
}

static fildes_t in;
static stat_t sbuf;

struct command sosh_commands[] = {
	{"dir", ls},
	{"ls", ls},
	{"cat", cat},
	{"cp", cp},
	{"rm", rm},
	{"ps", ps},
	{"exec", exec},
	{"segfault", segfault},
	{"sleep", sleep},
	{"up", up},
	{"help", help},
	{"time", time},
	{"m5bench", m5bench},
	{"pt_test", pt_test},
	{"kecho", kecho},
	{"null", NULL}
};

int
main(void)
{
	char buf[BUF_SIZ];
	char *argv[MAX_ARGS];
	int i, r, done, found, new, argc;
	char *bp, *p;

	in = open("console", FM_READ);
	assert (in >= 0);

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

		for (i = 0; i < sizeof(sosh_commands) / sizeof(struct command); i++) {
			if (strcmp(argv[0], sosh_commands[i].name) == 0) {
				sosh_commands[i].command(argc, argv);
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

	close(in);
	printf("[SOS Exiting]\n");
}

