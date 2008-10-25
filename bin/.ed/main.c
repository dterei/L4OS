/*  GNU ed - The GNU line editor.
	 Copyright (C) 2006, 2007, 2008 Antonio Diaz Diaz.

	 This program is free software: you can redistribute it and/or modify
	 it under the terms of the GNU General Public License as published by
	 the Free Software Foundation, either version 3 of the License, or
	 (at your option) any later version.

	 This program is distributed in the hope that it will be useful,
	 but WITHOUT ANY WARRANTY; without even the implied warranty of
	 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	 GNU General Public License for more details.

	 You should have received a copy of the GNU General Public License
	 along with this program.  If not, see <http://www.gnu.org/licenses/>.
	 */
/*
	Return values: 0 for a normal exit, 1 for environmental problems
	(file not found, invalid flags, I/O errors, etc), 2 to indicate a
	corrupt or invalid input file, 3 for an internal consistency error
	(eg, bug) which caused ed to panic.
	*/
/*
 * CREDITS
 *
 *      This program is based on the editor algorithm described in
 *      Brian W. Kernighan and P. J. Plauger's book "Software Tools
 *      in Pascal," Addison-Wesley, 1981.
 *
 *      The buffering algorithm is attributed to Rodney Ruddock of
 *      the University of Guelph, Guelph, Ontario.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <locale.h>
#include <sos/sos.h>

#include "carg_parser.h"
#include "ed.h"

fildes_t IN;

static const char *invocation_name = 0;
static const char *const Program_name = "GNU Ed";
static const char *const program_name = "ed";
static const char *const program_year = "2008";

static char _restricted = 0;	/* invoked as "red" */
static char _scripted = 0;	/* if set, suppress diagnostics */
static char _traditional = 0;	/* if set, be backwards compatible */


char restricted(void)
{
	return _restricted;
}

char scripted(void)
{
	return _scripted;
}

char traditional(void)
{
	return _traditional;
}


void show_help(void)
{
	printf("%s - The GNU line editor.\n", Program_name);
	printf("\nUsage: %s [options] [file]\n", invocation_name);
	printf("Options:\n");
	printf("  -h, --help                 display this help and exit\n");
	printf
		("  -V, --version              output version information and exit\n");
	printf("  -G, --traditional          run in compatibility mode\n");
	printf
		("  -l, --loose-exit-status    exit with 0 status even if a command fails\n");
	printf
		("  -p, --prompt=STRING        use STRING as an interactive prompt\n");
	printf("  -s, --quiet, --silent      suppress diagnostics\n");
	printf("  -v, --verbose              be verbose\n");
	printf
		("Start edit by reading in `file' if given. Read output of shell command\n");
	printf("if `file' begins with a `!'.\n");
	printf("\nReport bugs to <bug-ed@gnu.org>.\n");
	printf("Ed home page: http://www.gnu.org/software/ed/ed.html\n");
}


void show_version(void)
{
	printf("%s %s\n", Program_name, PROGVERSION);
	printf("Copyright (C) 1994 Andrew L. Moore, %s Antonio Diaz Diaz.\n",
			program_year);
	printf
		("License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n");
	printf
		("This is free software: you are free to change and redistribute it.\n");
	printf("There is NO WARRANTY, to the extent permitted by law.\n");
}


void show_strerror(const char *filename, int errcode)
{
	if (!_scripted) {
		if (filename && filename[0] != 0)
			fprintf(stderr, "%s: ", filename);
		fprintf(stderr, "%s\n", strerror(errcode));
	}
}


void show_error(const char *msg, const int errcode, const char help)
{
	if (msg && msg[0] != 0) {
		fprintf(stderr, "%s: %s", program_name, msg);
		if (errcode > 0)
			fprintf(stderr, ": %s", strerror(errcode));
		fprintf(stderr, "\n");
	}
	if (help && invocation_name && invocation_name[0] != 0)
		fprintf(stderr, "Try `%s --help' for more information.\n",
				invocation_name);
}


/* return true if file descriptor is a regular file */
char is_regular_file(int fd)
{
	struct stat sb;

	return (fstat(fd, &sb) < 0 || S_ISREG(sb.st_mode));
}


char is_valid_filename(const char *name)
{
	if (_restricted
			&& (*name == '!' || !strcmp(name, "..") || strchr(name, '/'))) {
		set_error_msg("Shell access restricted");
		return 0;
	}
	return 1;
}


int main(const int argc, const char *argv[])
{
	int n = strlen(argv[0]);
	char loose = 0;
	const ap_Option options[] = {
		{'G', "traditional", ap_no},
		{'h', "help", ap_no},
		{'l', "loose-exit-status", ap_no},
		{'p', "prompt", ap_yes},
		{'s', "quiet", ap_no},
		{'s', "silent", ap_no},
		{'v', "verbose", ap_no},
		{'V', "version", ap_no},
		{0, 0, ap_no}
	};
	Arg_parser parser;
	int argind;

	if (!ap_init(&parser, argc, argv, options, 0)) {
		show_error("Memory exhausted", 0, 0);
		return 1;
	}
	if (ap_error(&parser)) {	/* bad option */
		show_error(ap_error(&parser), 0, 1);
		return 1;
	}
	invocation_name = argv[0];
	_restricted = (n > 2 && argv[0][n - 3] == 'r');

	for (argind = 0; argind < ap_arguments(&parser); ++argind) {
		const int code = ap_code(&parser, argind);
		const char *arg = ap_argument(&parser, argind);

		if (!code)
			break;		/* no more options */
		switch (code) {
			case 'G':
				_traditional = 1;
				break;		/* backward compatibility */
			case 'h':
				show_help();
				return 0;
			case 'l':
				loose = 1;
				break;
			case 'p':
				set_prompt(arg);
				break;
			case 's':
				_scripted = 1;
				break;
			case 'v':
				set_verbose();
				break;
			case 'V':
				show_version();
				return 0;
			default:
				show_error("internal_error: uncaught option", 0, 0);
				return 3;
		}
	}
	setlocale(LC_ALL, "");
	if (!init_buffers())
		return 1;

	while (argind < ap_arguments(&parser)) {
		const char *arg = ap_argument(&parser, argind);

		if (!strcmp(arg, "-")) {
			_scripted = 1;
			++argind;
			continue;
		}
		if (is_valid_filename(arg)) {
			if (read_file(arg, 0) < 0 && is_regular_file(0))
				return 2;
			else if (arg[0] != '!')
				set_def_filename(arg);
		} else {
			fputs("?\n", stderr);
			if (arg[0])
				set_error_msg("Invalid filename");
			if (is_regular_file(0))
				return 2;
		}
		break;
	}
	ap_free(&parser);

	// Open the console for reading

	IN = open("console", FM_READ);
	if (IN < 0) {
		printf("Failed to open console for reading\n");
		exit(1);
	}

	return main_loop(loose);
}
