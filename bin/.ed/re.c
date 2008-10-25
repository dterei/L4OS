/* re.c: regular expression interface routines for the ed line editor. */
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

#include <stddef.h>
#include <errno.h>
#include <sys/types.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ed.h"


static regex_t *global_pat = 0;
static char patlock = 0;	/* if set, pattern not freed by get_compiled_pattern */

static char *rhbuf;		/* rhs substitution buffer */
static int rhbufsz;		/* rhs substitution buffer size */
static int rhbufi;		/* rhs substitution buffer index */

static char *rbuf;		/* substitute_matching_text buffer */
static int rbufsz;		/* substitute_matching_text buffer size */


char prev_pattern(void)
{
	return global_pat != 0;
}


/* translate characters in a string */
static void translit_text(char *s, int len, char from, char to)
{
	char *p = s;

	while (--len > 0) {
		if (*p == from)
			*p = to;
		++p;
	}
}


/* overwrite newlines with ASCII NULs */
void newline_to_nul(char *s, int len)
{
	translit_text(s, len, '\n', '\0');
}

/* overwrite ASCII NULs with newlines */
void nul_to_newline(char *s, int len)
{
	translit_text(s, len, '\0', '\n');
}


/* expand a POSIX character class */
const char *parse_char_class(const char *s)
{
	char c, d;

	if (*s == '^')
		++s;
	if (*s == ']')
		++s;
	for (; *s != ']' && *s != '\n'; ++s)
		if (*s == '[' && ((d = s[1]) == '.' || d == ':' || d == '='))
			for (++s, c = *++s; *s != ']' || c != d; ++s)
				if ((c = *s) == '\n')
					return 0;
	return ((*s == ']') ? s : 0);
}


/* copy a pattern string from the command buffer; return pointer to the copy */
char *extract_pattern(const char **ibufpp, const int delimiter)
{
	static char *lhbuf = 0;	/* buffer */
	static int lhbufsz = 0;	/* buffer size */
	const char *nd = *ibufpp;
	int len;

	while (*nd != delimiter && *nd != '\n') {
		if (*nd == '[') {
			nd = parse_char_class(++nd);
			if (!nd) {
				set_error_msg("Unbalanced brackets ([])");
				return 0;
			}
		} else if (*nd == '\\' && *++nd == '\n') {
			set_error_msg("Trailing backslash (\\)");
			return 0;
		}
		++nd;
	}
	len = nd - *ibufpp;
	if (!resize_buffer((void *) &lhbuf, &lhbufsz, len + 1))
		return 0;
	memcpy(lhbuf, *ibufpp, len);
	lhbuf[len] = 0;
	*ibufpp = nd;
	if (isbinary())
		nul_to_newline(lhbuf, len);
	return lhbuf;
}


/* return pointer to compiled pattern from command buffer */
regex_t *get_compiled_pattern(const char **ibufpp)
{
	static regex_t *exp = 0;
	char *exps;
	const char delimiter = **ibufpp;
	int n;

	if (delimiter == ' ') {
		set_error_msg("Invalid pattern delimiter");
		return 0;
	}
	if (delimiter == '\n' || *++(*ibufpp) == '\n' || **ibufpp == delimiter) {
		if (!exp)
			set_error_msg("No previous pattern");
		return exp;
	}
	if (!(exps = extract_pattern(ibufpp, delimiter)))
		return 0;
	/* buffer alloc'd && not reserved */
	if (exp && !patlock)
		regfree(exp);
	else if (!(exp = (regex_t *) malloc(sizeof(regex_t)))) {
		show_strerror(0, errno);
		set_error_msg("Memory exhausted");
		return 0;
	}
	patlock = 0;
	n = regcomp(exp, exps, 0);
	if (n) {
		char buf[80];

		regerror(n, exp, buf, sizeof(buf));
		set_error_msg(buf);
		free(exp);
		exp = 0;
	}
	return exp;
}


/* add line matching a pattern to the global-active list */
	char
build_active_list(const char **ibufpp, const int first_addr,
		const int second_addr, const char match)
{
	regex_t *pat;
	line_t *lp;
	int addr;
	const char delimiter = **ibufpp;

	if (delimiter == ' ' || delimiter == '\n') {
		set_error_msg("Invalid pattern delimiter");
		return 0;
	}
	if (!(pat = get_compiled_pattern(ibufpp)))
		return 0;
	if (**ibufpp == delimiter)
		++(*ibufpp);
	clear_active_list();
	lp = search_line_node(first_addr);
	for (addr = first_addr; addr <= second_addr; ++addr, lp = lp->q_forw) {
		char *s = get_sbuf_line(lp);

		if (!s)
			return 0;
		if (isbinary())
			nul_to_newline(s, lp->len);
		if (!regexec(pat, s, 0, 0, 0) == match && !set_active_node(lp))
			return 0;
	}
	return 1;
}


/* return pointer to copy of substitution template in the command buffer */
char *extract_subst_template(const char **ibufpp, const char isglobal)
{
	int i = 0, n = 0;
	char c;
	const char delimiter = **ibufpp;

	++(*ibufpp);
	if (**ibufpp == '%' && (*ibufpp)[1] == delimiter) {
		++(*ibufpp);
		if (!rhbuf)
			set_error_msg("No previous substitution");
		return rhbuf;
	}
	while (**ibufpp != delimiter) {
		if (!resize_buffer((void *) &rhbuf, &rhbufsz, i + 2))
			return 0;
		c = rhbuf[i++] = *(*ibufpp)++;
		if (c == '\n' && **ibufpp == 0) {
			--i, --(*ibufpp);
			break;
		}
		if (c == '\\' && (rhbuf[i++] = *(*ibufpp)++) == '\n' && !isglobal) {
			while ((*ibufpp = get_tty_line(&n)) &&
					(n == 0 || (n > 0 && (*ibufpp)[n - 1] != '\n')))
				clearerr(stdin);
			if (!(*ibufpp))
				return 0;
		}
	}
	if (!resize_buffer((void *) &rhbuf, &rhbufsz, i + 1))
		return 0;
	rhbuf[rhbufi = i] = 0;
	return rhbuf;
}


/* extract substitution tail from the command buffer */
	char
extract_subst_tail(const char **ibufpp, int *gflagsp, int *snump,
		const char isglobal)
{
	const char delimiter = **ibufpp;

	*gflagsp = *snump = 0;
	if (delimiter == '\n') {
		rhbufi = 0;
		*gflagsp = GPR;
		return 1;
	}
	if (!extract_subst_template(ibufpp, isglobal))
		return 0;
	if (**ibufpp == '\n') {
		*gflagsp = GPR;
		return 1;
	}
	if (**ibufpp == delimiter)
		++(*ibufpp);
	if (**ibufpp >= '1' && **ibufpp <= '9')
		return parse_int(snump, *ibufpp, ibufpp);
	if (**ibufpp == 'g') {
		++(*ibufpp);
		*gflagsp = GSG;
		return 1;
	}
	return 1;
}


/* return the address of the next line matching a pattern in a given
	direction. wrap around begin/end of editor buffer if necessary */
int get_matching_node_addr(const char **ibufpp, const char forward)
{
	regex_t *pat = get_compiled_pattern(ibufpp);
	int addr = current_addr();

	if (!pat)
		return -1;
	do {
		addr = (forward ? inc_addr(addr) : dec_addr(addr));
		if (addr) {
			line_t *lp = search_line_node(addr);
			char *s = get_sbuf_line(lp);

			if (!s)
				return -1;
			if (isbinary())
				nul_to_newline(s, lp->len);
			if (!regexec(pat, s, 0, 0, 0))
				return addr;
		}
	}
	while (addr != current_addr());
	set_error_msg("No match");
	return -1;
}


char new_compiled_pattern(const char **ibufpp)
{
	regex_t *tpat = global_pat;

	disable_interrupts();
	tpat = get_compiled_pattern(ibufpp);
	if (!tpat) {
		enable_interrupts();
		return 0;
	}
	if (tpat != global_pat) {
		if (global_pat) {
			regfree(global_pat);
			free(global_pat);
		}
		global_pat = tpat;
		patlock = 1;		/* reserve pattern */
	}
	enable_interrupts();
	return 1;
}


/* modify text according to a substitution template; return offset to
	end of modified text */
	int
apply_subst_template(const char *boln, const regmatch_t * rm, int off,
		const int re_nsub)
{
	const char *sub = rhbuf;

	for (; sub - rhbuf < rhbufi; ++sub) {
		int n;

		if (*sub == '&') {
			int j = rm[0].rm_so;
			int k = rm[0].rm_eo;

			if (!resize_buffer((void *) &rbuf, &rbufsz, off + k - j))
				return -1;
			while (j < k)
				rbuf[off++] = boln[j++];
		} else if (*sub == '\\' && *++sub >= '1' && *sub <= '9' &&
				(n = *sub - '0') <= re_nsub) {
			int j = rm[n].rm_so;
			int k = rm[n].rm_eo;

			if (!resize_buffer((void *) &rbuf, &rbufsz, off + k - j))
				return -1;
			while (j < k)
				rbuf[off++] = boln[j++];
		} else {
			if (!resize_buffer((void *) &rbuf, &rbufsz, off + 1))
				return -1;
			rbuf[off++] = *sub;
		}
	}
	if (!resize_buffer((void *) &rbuf, &rbufsz, off + 1))
		return -1;
	rbuf[off] = 0;
	return off;
}


/* replace text matched by a pattern according to a substitution
	template; return pointer to the modified text */
	int
substitute_matching_text(const line_t * lp, const int gflags, const int snum)
{
	const int se_max = 30;	/* max subexpressions in a regular expression */
	regmatch_t rm[se_max];
	char *txt = get_sbuf_line(lp);
	char *eot;
	int i = 0, off = 0;
	char changed = 0;

	if (!txt)
		return -1;
	if (isbinary())
		nul_to_newline(txt, lp->len);
	eot = txt + lp->len;
	if (!regexec(global_pat, txt, se_max, rm, 0)) {
		int matchno = 0;

		do {
			if (!snum || snum == ++matchno) {
				changed = 1;
				i = rm[0].rm_so;
				if (!resize_buffer((void *) &rbuf, &rbufsz, off + i))
					return -1;
				if (isbinary())
					newline_to_nul(txt, rm[0].rm_eo);
				memcpy(rbuf + off, txt, i);
				off += i;
				off = apply_subst_template(txt, rm, off, global_pat->re_nsub);
				if (off < 0)
					return -1;
			} else {
				i = rm[0].rm_eo;
				if (!resize_buffer((void *) &rbuf, &rbufsz, off + i))
					return -1;
				if (isbinary())
					newline_to_nul(txt, i);
				memcpy(rbuf + off, txt, i);
				off += i;
			}
			txt += rm[0].rm_eo;
		}
		while (*txt && (!changed || ((gflags & GSG) && rm[0].rm_eo)) &&
				!regexec(global_pat, txt, se_max, rm, REG_NOTBOL));
		i = eot - txt;
		if (!resize_buffer((void *) &rbuf, &rbufsz, off + i + 2))
			return -1;
		if (i > 0 && !rm[0].rm_eo && (gflags & GSG)) {
			set_error_msg("Infinite substitution loop");
			return -1;
		}
		if (isbinary())
			newline_to_nul(txt, i);
		memcpy(rbuf + off, txt, i);
		memcpy(rbuf + off + i, "\n", 2);
	}
	return (changed ? off + i + 1 : 0);
}


/* for each line in a range, change text matching a pattern according to
	a substitution template; return false if error */
	char
search_and_replace(const int first_addr, const int second_addr,
		const int gflags, const int snum, const char isglobal)
{
	int lc, nsubs = 0;

	set_current_addr(first_addr - 1);
	for (lc = 0; lc <= second_addr - first_addr; ++lc) {
		line_t *lp = search_line_node(inc_current_addr());
		int len = substitute_matching_text(lp, gflags, snum);

		if (len < 0)
			return 0;
		if (len) {
			const char *txt, *eot;
			undo_t *up = 0;

			if (!delete_lines(current_addr(), current_addr(), isglobal))
				return 0;
			txt = rbuf;
			eot = rbuf + len;
			disable_interrupts();
			do {
				txt = put_sbuf_line(txt, current_addr());
				if (!txt) {
					enable_interrupts();
					return 0;
				}
				if (up)
					up->tail = search_line_node(current_addr());
				else if (!(up = push_undo_stack(UADD, -1, -1))) {
					enable_interrupts();
					return 0;
				}
			}
			while (txt != eot);
			enable_interrupts();
			++nsubs;
		}
	}
	if (nsubs == 0 && !(gflags & GLB)) {
		set_error_msg("No match");
		return 0;
	}
	return 1;
}
