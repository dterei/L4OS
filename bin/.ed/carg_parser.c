/*  Arg_parser - A POSIX/GNU command line argument parser. C version
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

#include <stdlib.h>
#include <string.h>

#include "carg_parser.h"


/* assure at least a minimum size for buffer `buf' */
char ap_resize_buffer(void *buf, const int min_size)
{
	void *new_buf = 0;

	if (*(void **) buf)
		new_buf = realloc(*(void **) buf, min_size);
	else
		new_buf = malloc(min_size);
	if (!new_buf)
		return 0;
	*(void **) buf = new_buf;
	return 1;
}


char push_back_record(Arg_parser * ap, const int code, const char *argument)
{
	const int len = strlen(argument);
	ap_Record *p;

	if (!ap_resize_buffer((void *) &(ap->data),
				(ap->data_size + 1) * sizeof(ap_Record)))
		return 0;
	p = &(ap->data[ap->data_size]);
	p->code = code;
	p->argument = 0;
	if (!ap_resize_buffer((void *) &(p->argument), len + 1))
		return 0;
	strncpy(p->argument, argument, len + 1);
	++ap->data_size;
	return 1;
}


char add_error(Arg_parser * ap, const char *msg)
{
	const int len = strlen(msg);

	if (!ap_resize_buffer((void *) &(ap->error), ap->error_size + len + 1))
		return 0;
	strncpy(ap->error + ap->error_size, msg, len + 1);
	ap->error_size += len;
	return 1;
}


void free_data(Arg_parser * ap)
{
	int i;

	for (i = 0; i < ap->data_size; ++i)
		free(ap->data[i].argument);
	if (ap->data) {
		free(ap->data);
		ap->data = 0;
	}
	ap->data_size = 0;
}


	char
parse_long_option(Arg_parser * ap,
		const char *const opt, const char *const arg,
		const ap_Option options[], int *argindp)
{
	unsigned int len;
	int index = -1;
	int i;
	char exact = 0, ambig = 0;

	for (len = 0; opt[len + 2] && opt[len + 2] != '='; ++len);

	// Test all long options for either exact match or abbreviated matches.
	for (i = 0; options[i].code != 0; ++i)
		if (options[i].name && !strncmp(options[i].name, &opt[2], len)) {
			if (strlen(options[i].name) == len)	// Exact match found
			{
				index = i;
				exact = 1;
				break;
			} else if (index < 0)
				index = i;	// First nonexact match found
			else if (options[index].code != options[i].code ||
					options[index].has_arg != options[i].has_arg)
				ambig = 1;	// Second or later nonexact match found
		}

	if (ambig && !exact) {
		add_error(ap, "option `");
		add_error(ap, opt);
		add_error(ap, "' is ambiguous");
		return 1;
	}

	if (index < 0)		// nothing found
	{
		add_error(ap, "unrecognized option `");
		add_error(ap, opt);
		add_error(ap, "'");
		return 1;
	}

	++*argindp;

	if (opt[len + 2])		// `--<long_option>=<argument>' syntax
	{
		if (options[index].has_arg == ap_no) {
			add_error(ap, "option `--");
			add_error(ap, options[index].name);
			add_error(ap, "' doesn't allow an argument");
			return 1;
		}
		if (options[index].has_arg == ap_yes && !opt[len + 3]) {
			add_error(ap, "option `--");
			add_error(ap, options[index].name);
			add_error(ap, "' requires an argument");
			return 1;
		}
		return push_back_record(ap, options[index].code, &opt[len + 3]);
	}

	if (options[index].has_arg == ap_yes) {
		if (!arg) {
			add_error(ap, "option `--");
			add_error(ap, options[index].name);
			add_error(ap, "' requires an argument");
			return 1;
		}
		++*argindp;
		return push_back_record(ap, options[index].code, arg);
	}

	return push_back_record(ap, options[index].code, "");
}


	char
parse_short_option(Arg_parser * ap,
		const char *const opt, const char *const arg,
		const ap_Option options[], int *argindp)
{
	int cind = 1;		// character index in opt

	while (cind > 0) {
		int index = -1;
		int i;
		const unsigned char code = opt[cind];
		const char code_str[2] = { code, 0 };

		if (code != 0)
			for (i = 0; options[i].code; ++i)
				if (code == options[i].code) {
					index = i;
					break;
				}

		if (index < 0) {
			add_error(ap, "invalid option -- ");
			add_error(ap, code_str);
			return 1;
		}

		if (opt[++cind] == 0) {
			++*argindp;
			cind = 0;
		}			// opt finished

		if (options[index].has_arg != ap_no && cind > 0 && opt[cind]) {
			if (!push_back_record(ap, code, &opt[cind]))
				return 0;
			++*argindp;
			cind = 0;
		} else if (options[index].has_arg == ap_yes) {
			if (!arg || !arg[0]) {
				add_error(ap, "option requires an argument -- ");
				add_error(ap, code_str);
				return 1;
			}
			++*argindp;
			cind = 0;
			if (!push_back_record(ap, code, arg))
				return 0;
		} else if (!push_back_record(ap, code, ""))
			return 0;
	}
	return 1;
}


	char
ap_init(Arg_parser * ap, const int argc, const char *const argv[],
		const ap_Option options[], const char in_order)
{
	const char **non_options = 0;	// skipped non-options
	int non_options_size = 0;	// number of skipped non-options
	int argind = 1;		// index in argv
	int i;

	ap->data = 0;
	ap->error = 0;
	ap->data_size = 0;
	ap->error_size = 0;
	if (argc < 2 || !argv || !options)
		return 1;

	while (argind < argc) {
		const unsigned char ch1 = argv[argind][0];
		const unsigned char ch2 = (ch1 ? argv[argind][1] : 0);

		if (ch1 == '-' && ch2)	// we found an option
		{
			const char *const opt = argv[argind];
			const char *const arg = (argind + 1 < argc) ? argv[argind + 1] : 0;

			if (ch2 == '-') {
				if (!argv[argind][2]) {
					++argind;
					break;
				}		// we found "--"
				else if (!parse_long_option(ap, opt, arg, options, &argind))
					return 0;
			} else if (!parse_short_option(ap, opt, arg, options, &argind))
				return 0;
			if (ap->error)
				break;
		} else {
			if (!in_order) {
				if (!ap_resize_buffer((void *) &non_options,
							(non_options_size +
							 1) * sizeof(*non_options)))
					return 0;
				non_options[non_options_size++] = argv[argind++];
			} else if (!push_back_record(ap, 0, argv[argind++]))
				return 0;
		}
	}
	if (ap->error)
		free_data(ap);
	else {
		for (i = 0; i < non_options_size; ++i)
			if (!push_back_record(ap, 0, non_options[i]))
				return 0;
		while (argind < argc)
			if (!push_back_record(ap, 0, argv[argind++]))
				return 0;
	}
	if (non_options)
		free(non_options);
	return 1;
}


void ap_free(Arg_parser * ap)
{
	free_data(ap);
	if (ap->error) {
		free(ap->error);
		ap->error = 0;
	}
	ap->error_size = 0;
}


const char *ap_error(const Arg_parser * ap)
{
	return ap->error;
}


int ap_arguments(const Arg_parser * ap)
{
	return ap->data_size;
}


int ap_code(const Arg_parser * ap, const int i)
{
	if (i >= 0 && i < ap_arguments(ap))
		return ap->data[i].code;
	else
		return 0;
}


const char *ap_argument(const Arg_parser * ap, const int i)
{
	if (i >= 0 && i < ap_arguments(ap))
		return ap->data[i].argument;
	else
		return "";
}
