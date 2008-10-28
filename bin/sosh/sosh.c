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

#define verbose 1

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
	char *fout = NULL, *ferr = NULL, *fin = NULL;
	fildes_t fdout = VFS_NIL_FILE, fderr = VFS_NIL_FILE, fdin = VFS_NIL_FILE;

	if (argc < 2) {
		printf("Usage: exec filename [&]\n");
		return 1;
	}

	for (int i = 2; i < argc; i++) {
		if (strcmp(argv[i], "&") == 0) {
			bg = 1;
		// case of '> file'
		} else if (strcmp(argv[i], ">") == 0 || strcmp(argv[i], "1>") == 0) {
			if (!(i + 1 < argc)) {
				printf("Stdout not specified\n");
				return 1;
			}
			i++;
			fout = &argv[i][0];
		// case of '>file'
		} else if (strncmp(argv[i], "1>", 2) == 0) {
			if (strlen(argv[i]) < 3) {
				printf("stdout not specified\n");
				return 1;
			}
			fout = &argv[i][2];
		} else if (strncmp(argv[i], ">", 1) == 0) {
			if (strlen(argv[i]) < 2) {
				printf("stdout not specified\n");
				return 1;
			}
			fout = &argv[i][1];
		// case of '< file'
		} else if (strcmp(argv[i], "<") == 0) {
			if (!(i + 1 < argc)) {
				printf("Stdin not specified\n");
				return 1;
			}
			i++;
			fin = &argv[i][0];
		// case of '<file'
		} else if (strncmp(argv[i], "<", 1) == 0) {
			if (strlen(argv[i]) < 2) {
				printf("Stdin not specified\n");
				return 1;
			}
			fin = &argv[i][1];
		}

	}

	char *error_msg = NULL;
	if (bg == 0) {
		r = close(in);
		if (r != 0) {
			exitFailure("can't close console\n");
		}
	}

	if (fout != NULL) {
		fdout = open(fout, FM_WRITE);
		if (fdout < 0) {
			error_msg = "Can't open stdout!\n";
		}
	}

	if (ferr != NULL) {
		fderr = open(ferr, FM_WRITE);
		if (fderr < 0) {
			error_msg = "Can't open stdout!\n";
		}
	}

	if (fin != NULL) {
		fdin = open(fin, FM_READ);
		if (fdin < 0) {
			error_msg = "Can't open stdout!\n";
		}
	}

	if (error_msg == NULL) {
		pid = process_create2(argv[1], fdout, fderr, fdin);

		if (pid >= 0) {
			if (bg == 0) {
				process_wait(pid);
			}
		} else {
			printf("Failed!\n");
		}
	}

	if (bg == 0) {
		in = open("console", FM_READ);
		if (in < 0) {
			exitFailure("can't open console for reading");
		}
	}

	if (error_msg != NULL) {
		printf(error_msg);
		return 1;
	}

	if (fdout != VFS_NIL_FILE) close(fdout);
	if (fderr != VFS_NIL_FILE) close(fderr);
	if (fdin != VFS_NIL_FILE) close(fdin);

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

	if (num_read != SOS_VFS_EOF && num_read < 0) {
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

	if (verbose > 1) {
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

	if ((num_read != SOS_VFS_EOF && num_read < 0) || num_written < 0) {
		close(fd);
		close(fd_out);
		printf("error on cp: %s\n", sos_error_msg(fd));
		return 1;
	}

	close(fd);
	close(fd_out);
	return 0;
}

static int rm(int argc, char *argv[]) {
	if (argc != 2) {
		printf("usage %s [file]\n", argv[0]);
		return 1;
	}

	int r = fremove(argv[1]);

	if (r < 0) {
		printf("rm(%s) failed: %d\n", argv[1], r);
		printf("Can't remove file, %s\n", sos_error_msg(r));
	}

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

static int sosh_exit(int argc, char **argv) {
	printf("Sosh (%d) exiting...\n", my_id());
	exit(EXIT_SUCCESS);
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

static int segfault(int argc, char **argv) {
	int *null = NULL;
	return *null;
}

static int sleep(int argc, char *argv[]) {
	if (argc < 2) {
		printf("usage: %s msec\n", argv[0]);
		return 1;
	}

	int msec = atoi(argv[1]);
	usleep(msec * 1000);
	return 0;
}

static int echo(int argc, char *argv[]) {
	if (argc < 2) {
		printf("\n");
	} else {
		for (int i = 1; i < argc; i++) {
			printf("%s ", argv[i]);
		}
		if (argv[argc - 1][strlen(argv[argc - 1])] != '\n') {
			printf("\n");
		}
	}
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
	{"echo", echo},
	{"exec", exec},
	{"exit", sosh_exit},
	{"help", help},
	{"kill", kill},
	{"ls", ls},
	{"pid", pid},
	{"rm", rm},
	{"segfault", segfault},
	{"sleep", sleep},
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
	char line[IO_MAX_BUFFER];
	char *argv[32];
	int i, r, done, found, new, lineDone, lineMore, argc;
	char *bp, *p = "", *next;

	in = open("console", FM_READ);
	if (in < 0) {
		printf("%s\n", sos_error_msg(in));	
		exitFailure("can't open console for reading");
	}

	bp  = buf;
	next = buf;
	new = 1;
	lineDone = 1;
	lineMore = 0;
	done = 0;
	r = 0;

	printf("\n[SOS Starting]\n");

	while (!done) {
		if (new) {
			printf("$ ");
		}
		new   = 0;
		found = 0;

		while (!found && !done) {
			if (lineDone || lineMore) {
				if (verbose > 1) {
					printf("reading from stdin\n");
				}
				r = read(in, bp, IO_MAX_BUFFER-1+buf-bp);
				next = bp;
				lineDone = 0;
				lineMore = 0;
			}
			if (r == SOS_VFS_EOF) {
				sosh_exit(0, NULL);
			}
			if (r<0) {
				printf("Console read failed!\n");
				printf("%s\n", sos_error_msg(r));	
				done=1;
				break;
			}
			bp[r] = '\0';		/* terminate */
			if (verbose > 1) {
				printf("sosh: just read %s, %d\n", bp, r);
				if (bp[r-1] != '\n') {
					printf("\n");
				}
			}
			for (p=next; p<bp+r; p++) {
				if (*p == '\03') {	/* ^C */
					printf("^C\n");
					p   = buf;
					new = 1;
					lineDone = 1;
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
					*p    = '\0';
					found = p>buf;
					new   = 1;
					strncpy(line, next, p - next + 1);
					if (p - next + 1 == r) {
						if (verbose > 1) {
							printf("line done\n");
						}
						lineDone = 1;
					} else {
						printf("\n");
					}
					next = p + 1;
					break;
				} else if (verbose > 2)  {
					printf("%c",*p);
				}
			}
			if (verbose > 2) {
				printf("Get more line\n");
			}
			if (!found && !done) {
				lineMore = 1;
			}
		}
		if (verbose > 2) {
			printf("input grabbed\n");
		}

		if (!found) {
			if (verbose > 2) {
				printf("input not found, get new line\n");
			}
			lineDone = 1;
			continue;
		}

		argc = 0;
		p = line;

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

		if (verbose > 1) {
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

