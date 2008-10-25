/* signal.c: signal and miscellaneous routines for the ed line editor. */
/*  GNU ed - The GNU line editor.
	 Copyright (C) 1993, 1994 Andrew Moore, Talke Studio
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

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "ed.h"


static int _window_lines = 22;	/* scroll length: ws_row - 2 */
static int _window_columns = 72;

void set_window_lines(const int lines)
{
	_window_lines = lines;
}

int window_columns(void)
{
	return _window_columns;
}

int window_lines(void)
{
	return _window_lines;
}


/* convert a string to int with out_of_range detection */
char parse_int(int *i, const char *str, const char **tail)
{
	char *tmp;

	errno = 0;
	*i = strtol(str, &tmp, 10);
	if (tail)
		*tail = tmp;
	if (tmp == str) {
		set_error_msg("Bad numerical result");
		*i = 0;
		return 0;
	}
	if (errno == ERANGE) {
		set_error_msg("Numerical result out of range");
		*i = 0;
		return 0;
	}
	return 1;
}


/* assure at least a minimum size for buffer `buf' */
char resize_buffer(void *buf, int *size, int min_size)
{
	if (*size < min_size) {
		const int new_size = (min_size < 512 ? 512 : (min_size / 512) * 1024);
		void *new_buf = 0;

		disable_interrupts();
		if (*(void **) buf)
			new_buf = realloc(*(void **) buf, new_size);
		else
			new_buf = malloc(new_size);
		if (!new_buf) {
			show_strerror(0, errno);
			set_error_msg("Memory exhausted");
			enable_interrupts();
			return 0;
		}
		*size = new_size;
		*(void **) buf = new_buf;
		enable_interrupts();
	}
	return 1;
}


/* scan command buffer for next non-space char */
const char *skip_blanks(const char *s)
{
	while (isspace((unsigned char) *s) && *s != '\n')
		++s;
	return s;
}


/* return unescaped copy of escaped string */
const char *strip_escapes(const char *s)
{
	static char *file = 0;
	static int filesz = 0;
	const int len = strlen(s);

	int i = 0;

	if (!resize_buffer((void *) &file, &filesz, len + 1))
		return 0;
	/* assert: no trailing escape */
	while ((file[i++] = ((*s == '\\') ? *++s : *s)))
		s++;
	return file;
}
