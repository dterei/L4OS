/* glbl.c: global command routines for the ed line editor */
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ed.h"


static const line_t **active_list = 0;	/* list of lines active in a global command */
static int active_size = 0;	/* size (in bytes) of active_list */
static int active_last = 0;	/* index of last active line in active_list */
static int active_ptr = 0;	/* active_list index ( non-decreasing ) */
static int active_ndx = 0;	/* active_list index ( modulo active_last ) */


/* add a line node to the global-active list */
char set_active_node(const line_t * lp)
{
	disable_interrupts();
	if (!resize_buffer((void *) &active_list, &active_size,
				(active_last + 1) * sizeof(line_t **))) {
		show_strerror(0, errno);
		set_error_msg("Memory exhausted");
		enable_interrupts();
		return 0;
	}
	enable_interrupts();
	active_list[active_last++] = lp;
	return 1;
}


/* remove a range of lines from the global-active list */
void unset_active_nodes(const line_t * np, const line_t * mp)
{
	const line_t *lp = np;

	while (lp != mp) {
		int i;

		for (i = 0; i < active_last; ++i) {
			if (++active_ndx >= active_last)
				active_ndx = 0;
			if (active_list[active_ndx] == lp) {
				active_list[active_ndx] = 0;
				break;
			}
		}
		lp = lp->q_forw;
	}
}


/* return the next global-active line node */
const line_t *next_active_node(void)
{
	while (active_ptr < active_last && !active_list[active_ptr])
		++active_ptr;
	return (active_ptr < active_last) ? active_list[active_ptr++] : 0;
}


/* clear the global-active list */
void clear_active_list(void)
{
	disable_interrupts();
	if (active_list)
		free(active_list);
	active_list = 0;
	active_size = active_last = active_ptr = active_ndx = 0;
	enable_interrupts();
}
