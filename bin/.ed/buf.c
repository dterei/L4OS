/* buf.c: scratch-file buffer routines for the ed line editor. */
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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sos/sos.h>
//#include <unistd.h>
//#include <sys/file.h>
//#include <sys/stat.h>

#include "ed.h"

static int _current_addr;	/* current address in editor buffer */
static int _last_addr;		/* last address in editor buffer */
static char _isbinary;		/* if set, buffer contains ASCII NULs */
static char _modified;		/* if set, buffer modified since last write */
static char _newline_added;	/* if set, newline appended to input file */

static int seek_write;		/* seek before writing */
//static FILE *sfp;		/* scratch file pointer */
static fildes_t sfp;
static long sfpos;		/* scratch file position */
static line_t buffer_head;	/* editor buffer ( linked list of line_t ) */
static line_t yank_buffer_head;


int current_addr(void)
{
	return _current_addr;
}

int inc_current_addr(void)
{
	if (++_current_addr > _last_addr)
		_current_addr = _last_addr;
	return _current_addr;
}

void set_current_addr(const int addr)
{
	_current_addr = addr;
}

int last_addr(void)
{
	return _last_addr;
}

char isbinary(void)
{
	return _isbinary;
}

void set_binary(void)
{
	_isbinary = 1;
}

char modified(void)
{
	return _modified;
}

void set_modified(const char m)
{
	_modified = m;
}

char newline_added(void)
{
	return _newline_added;
}

void set_newline_added(void)
{
	_newline_added = 1;
}


int inc_addr(int addr)
{
	if (++addr > _last_addr)
		addr = 0;
	return addr;
}

int dec_addr(int addr)
{
	if (--addr < 0)
		addr = _last_addr;
	return addr;
}


/* link next and previous nodes */
void link_nodes(line_t * prev, line_t * next)
{
	prev->q_forw = next;
	next->q_back = prev;
}


/* insert node into circular queue after previous */
void insert_node(line_t * node, line_t * prev)
{
	link_nodes(node, prev->q_forw);
	link_nodes(prev, node);
}


/* remove node from circular queue */
//void remove_node( const line_t *node )
//  { link_nodes( node->q_back, node->q_forw ); }


/* add a line node in the editor buffer after the given line */
void add_line_node(line_t * lp, const int addr)
{
	line_t *p = search_line_node(addr);

	insert_node(lp, p);
	++_last_addr;
}


/* return a pointer to a copy of a line node, or to a new node if lp == 0 */
line_t *dup_line_node(line_t * lp)
{
	line_t *p = (line_t *) malloc(sizeof(line_t));

	if (!p) {
		show_strerror(0, errno);
		set_error_msg("Memory exhausted");
		return 0;
	}
	if (lp) {
		p->pos = lp->pos;
		p->len = lp->len;
	}
	return p;
}


/* insert text from stdin to after line n; stop when either a single
	period is read or EOF */
char append_lines(const char *ibufp2, const int addr, const char isglobal)
{
	int len;
	const char *txt = ibufp2;
	const char *eot;
	undo_t *up = 0;

	_current_addr = addr;

	while (1) {
		if (!isglobal) {
			ibufp2 = get_tty_line(&len);
			if (!ibufp2)
				return 0;
			if (!len || ibufp2[len - 1] != '\n') {
				return !len;
			}
			txt = ibufp2;
		} else if (!*(txt = ibufp2))
			return 1;
		else {
			while (*ibufp2++ != '\n');
			len = ibufp2 - txt;
		}
		if (len == 2 && txt[0] == '.' && txt[1] == '\n')
			return 1;
		eot = txt + len;
		do {
			txt = put_sbuf_line(txt, _current_addr);
			if (!txt) {
				return 0;
			}
			if (up)
				up->tail = search_line_node(_current_addr);
			else if (!(up = push_undo_stack(UADD, -1, -1))) {
				return 0;
			}
		}
		while (txt != eot);
		set_modified(1);
	}
}


void clear_yank_buffer(void)
{
	line_t *lp = yank_buffer_head.q_forw;

	while (lp != &yank_buffer_head) {
		line_t *cp = lp->q_forw;

		link_nodes(lp->q_back, lp->q_forw);
		free(lp);
		lp = cp;
	}
}


/* close scratch file */
char close_sbuf(void)
{
	clear_yank_buffer();
	clear_undo_stack();
	if (sfp) {
		//if (fclose(sfp) < 0) {
		if (close(sfp) < 0) {
			show_strerror(0, errno);
			set_error_msg("Cannot close temp file");
			return 0;
		}
		sfp = 0;
	}
	sfpos = 0;
	seek_write = 0;
	return 1;
}


/* copy a range of lines; return false if error */
char copy_lines(const int first_addr, const int second_addr, const int addr)
{
	line_t *lp, *np = search_line_node(first_addr);
	undo_t *up = 0;
	int n = second_addr - first_addr + 1;
	int m = 0;

	_current_addr = addr;
	if (addr >= first_addr && addr < second_addr) {
		n = addr - first_addr + 1;
		m = second_addr - addr;
	}
	for (; n > 0; n = m, m = 0, np = search_line_node(_current_addr + 1))
		for (; n-- > 0; np = np->q_forw) {
			lp = dup_line_node(np);
			if (!lp) {
				return 0;
			}
			add_line_node(lp, _current_addr++);
			if (up)
				up->tail = lp;
			else if (!(up = push_undo_stack(UADD, -1, -1))) {
				return 0;
			}
			_modified = 1;
		}
	return 1;
}


/* delete a range of lines */
char delete_lines(const int from, const int to, const char isglobal)
{
	line_t *n, *p;

	if (!yank_lines(from, to))
		return 0;
	if (!push_undo_stack(UDEL, from, to)) {
		return 0;
	}
	n = search_line_node(inc_addr(to));
	p = search_line_node(from - 1);	/* this search_line_node last! */
	if (isglobal)
		unset_active_nodes(p->q_forw, n);
	link_nodes(p, n);
	_last_addr -= to - from + 1;
	_current_addr = from - 1;
	_modified = 1;
	return 1;
}


/* return line number of pointer */
int get_line_node_addr(const line_t * lp)
{
	const line_t *cp = &buffer_head;
	int addr = 0;

	while (cp != lp && (cp = cp->q_forw) != &buffer_head)
		++addr;
	if (addr && cp == &buffer_head) {
		set_error_msg("Invalid address");
		return -1;
	}
	return addr;
}


/* get a line of text from the scratch file; return pointer to the text */
char *get_sbuf_line(const line_t * lp)
{
	static char *sfbuf = 0;	/* buffer */
	static int sfbufsz = 0;	/* buffer size */

	int len, ct;

	if (lp == &buffer_head)
		return 0;
	seek_write = 1;		/* force seek on write */
	/* out of position */
	if (sfpos != lp->pos) {
		sfpos = lp->pos;
		if (fseek(sfp, sfpos, SEEK_SET) < 0) {
			show_strerror(0, errno);
			set_error_msg("Cannot seek temp file");
			return 0;
		}
	}
	len = lp->len;
	if (!resize_buffer((void *) &sfbuf, &sfbufsz, len + 1))
		return 0;
	//ct = fread(sfbuf, 1, len, sfp);
	ct = read(IN, sfbuf, len);

	if (ct < 0 || ct != len) {
		show_strerror(0, errno);
		set_error_msg("Cannot read temp file");
		return 0;
	}
	sfpos += len;		/* update file position */
	sfbuf[len] = 0;
	return sfbuf;
}


/* open scratch buffer; initialize line queue */
char init_buffers(void)
{
	/* Read stdin one character at a time to avoid i/o contention
		with shell escapes invoked by nonterminal input, e.g.,
		ed - <<EOF
		!cat
		hello, world
		EOF */
	//setvbuf(stdin, 0, _IONBF, 0);
	if (!open_sbuf())
		return 0;
	link_nodes(&buffer_head, &buffer_head);
	link_nodes(&yank_buffer_head, &yank_buffer_head);
	return 1;
}


/* replace a range of lines with the joined text of those lines */
char join_lines(const int from, const int to, const char isglobal)
{
	static char *buf = 0;
	static int bsize;
	int size = 0;
	line_t *ep = search_line_node(inc_addr(to));
	line_t *bp = search_line_node(from);

	while (bp != ep) {
		char *s = get_sbuf_line(bp);

		if (!s || !resize_buffer((void *) &buf, &bsize, size + bp->len))
			return 0;
		memcpy(buf + size, s, bp->len);
		size += bp->len;
		bp = bp->q_forw;
	}
	if (!resize_buffer((void *) &buf, &bsize, size + 2))
		return 0;
	memcpy(buf + size, "\n", 2);
	if (!delete_lines(from, to, isglobal))
		return 0;
	_current_addr = from - 1;
	if (!put_sbuf_line(buf, _current_addr)
			|| !push_undo_stack(UADD, -1, -1)) {
		return 0;
	}
	set_modified(1);
	return 1;
}


/* move a range of lines */
	char
move_lines(const int first_addr, const int second_addr, const int addr,
		const char isglobal)
{
	line_t *b1, *a1, *b2, *a2;
	int n = inc_addr(second_addr);
	int p = first_addr - 1;

	if (addr == first_addr - 1 || addr == second_addr) {
		a2 = search_line_node(n);
		b2 = search_line_node(p);
		_current_addr = second_addr;
	} else if (!push_undo_stack(UMOV, p, n) ||
			!push_undo_stack(UMOV, addr, inc_addr(addr))) {
		return 0;
	} else {
		a1 = search_line_node(n);
		if (addr < first_addr) {
			b1 = search_line_node(p);
			b2 = search_line_node(addr);	/* this search_line_node last! */
		} else {
			b2 = search_line_node(addr);
			b1 = search_line_node(p);	/* this search_line_node last! */
		}
		a2 = b2->q_forw;
		link_nodes(b2, b1->q_forw);
		link_nodes(a1->q_back, a2);
		link_nodes(b1, a1);
		_current_addr = addr + ((addr < first_addr) ?
				second_addr - first_addr + 1 : 0);
	}
	if (isglobal)
		unset_active_nodes(b2->q_forw, a2);
	_modified = 1;
	return 1;
}


/* open scratch file */
char open_sbuf(void)
{
	_isbinary = _newline_added = 0;
	sfp = tmpfile();
	if (!sfp) {
		show_strerror(0, errno);
		set_error_msg("Cannot open temp file");
		return 0;
	}
	return 1;
}


int path_max(const char *filename)
{
	long result;

	if (!filename)
		filename = "/";
	errno = 0;
	result = pathconf(filename, _PC_PATH_MAX);
	if (result < 0) {
		if (errno)
			result = 256;
		else
			result = 1024;
	} else if (result < 256)
		result = 256;
	return result;
}


/* append lines from the yank buffer */
char put_lines(const int addr)
{
	undo_t *up = 0;
	line_t *lp = yank_buffer_head.q_forw, *cp;

	if (lp == &yank_buffer_head) {
		set_error_msg("Nothing to put");
		return 0;
	}
	_current_addr = addr;
	while (lp != &yank_buffer_head) {
		if (!(cp = dup_line_node(lp))) {
			return 0;
		}
		add_line_node(cp, _current_addr++);
		if (up)
			up->tail = cp;
		else if (!(up = push_undo_stack(UADD, -1, -1))) {
			return 0;
		}
		_modified = 1;
		lp = lp->q_forw;
	}
	return 1;
}


/* write a line of text to the scratch file and add a line node to the
	editor buffer; return a pointer to the end of the text */
const char *put_sbuf_line(const char *cs, const int addr)
{
	line_t *lp = dup_line_node(0);
	int len, ct;
	const char *s = cs;

	if (!lp)
		return 0;
	while (*s != '\n')
		++s;			/* assert: cs is '\n' terminated */
	if (s - cs >= INT_MAX) {	/* max chars per line */
		set_error_msg("Line too long");
		return 0;
	}
	len = s - cs;
	/* out of position */
	if (seek_write) {
		if (fseek(sfp, 0L, SEEK_END) < 0) {
			show_strerror(0, errno);
			set_error_msg("Cannot seek temp file");
			return 0;
		}
		sfpos = ftell(sfp);
		seek_write = 0;
	}
	ct = fwrite(cs, 1, len, sfp);	/* assert: interrupts disabled */
	if (ct < 0 || ct != len) {
		sfpos = -1;
		show_strerror(0, errno);
		set_error_msg("Cannot write temp file");
		return 0;
	}
	lp->len = len;
	lp->pos = sfpos;
	add_line_node(lp, addr);
	++_current_addr;
	sfpos += len;		/* update file position */
	return ++s;
}


/* return pointer to a line node in the editor buffer */
line_t *search_line_node(const int addr)
{
	static line_t *lp = &buffer_head;
	static int o_addr = 0;

	if (o_addr < addr) {
		if (o_addr + _last_addr >= 2 * addr)
			while (o_addr < addr) {
				++o_addr;
				lp = lp->q_forw;
			} else {
				lp = buffer_head.q_back;
				o_addr = _last_addr;
				while (o_addr > addr) {
					--o_addr;
					lp = lp->q_back;
				}
			}
	} else if (o_addr <= 2 * addr)
		while (o_addr > addr) {
			--o_addr;
			lp = lp->q_back;
		} else {
			lp = &buffer_head;
			o_addr = 0;
			while (o_addr < addr) {
				++o_addr;
				lp = lp->q_forw;
			}
		}
	return lp;
}


/* copy a range of lines to the cut buffer */
char yank_lines(const int from, const int to)
{
	line_t *ep = search_line_node(inc_addr(to));
	line_t *bp = search_line_node(from);
	line_t *lp = &yank_buffer_head;
	line_t *cp;

	clear_yank_buffer();
	while (bp != ep) {
		cp = dup_line_node(bp);
		if (!cp) {
			return 0;
		}
		insert_node(cp, lp);
		bp = bp->q_forw;
		lp = cp;
	}
	return 1;
}


static undo_t *ustack = 0;	/* undo stack */
static int usize = 0;		/* ustack size (in bytes) */
static int u_ptr = 0;		/* undo stack pointer */
static int u_current_addr = -1;	/* if >= 0, undo enabled */
static int u_addr_last = -1;	/* if >= 0, undo enabled */


void clear_undo_stack(void)
{
	while (u_ptr--)
		if (ustack[u_ptr].type == UDEL) {
			line_t *ep = ustack[u_ptr].tail->q_forw;
			line_t *lp = ustack[u_ptr].head;

			while (lp != ep) {
				line_t *tl = lp->q_forw;

				unmark_line_node(lp);
				free(lp);
				lp = tl;
			}
		}
	u_ptr = 0;
	u_current_addr = _current_addr;
	u_addr_last = _last_addr;
}


void disable_undo(void)
{
	clear_undo_stack();
	u_current_addr = u_addr_last = -1;
}


/* undo last change to the editor buffer */
char pop_undo_stack(const char isglobal)
{
	int n;
	int o_current_addr = _current_addr;
	int o_addr_last = _last_addr;

	if (u_current_addr < 0 || u_addr_last < 0) {
		set_error_msg("Nothing to undo");
		return 0;
	}
	if (u_ptr)
		set_modified(1);
	search_line_node(0);	/* reset cached value */
	for (n = u_ptr - 1; n >= 0; --n) {
		switch (ustack[n].type) {
			case UADD:
				link_nodes(ustack[n].head->q_back, ustack[n].tail->q_forw);
				break;
			case UDEL:
				link_nodes(ustack[n].head->q_back, ustack[n].head);
				link_nodes(ustack[n].tail, ustack[n].tail->q_forw);
				break;
			case UMOV:
			case VMOV:
				link_nodes(ustack[n - 1].head, ustack[n].head->q_forw);
				link_nodes(ustack[n].tail->q_back, ustack[n - 1].tail);
				link_nodes(ustack[n].head, ustack[n].tail);
				--n;
				break;
		}
		ustack[n].type ^= 1;
	}
	/* reverse undo stack order */
	for (n = 0; 2 * n < u_ptr - 1; ++n) {
		undo_t tmp = ustack[n];

		ustack[n] = ustack[u_ptr - 1 - n];
		ustack[u_ptr - 1 - n] = tmp;
	}
	if (isglobal)
		clear_active_list();
	_current_addr = u_current_addr;
	u_current_addr = o_current_addr;
	_last_addr = u_addr_last;
	u_addr_last = o_addr_last;
	return 1;
}


/* return pointer to intialized undo node */
undo_t *push_undo_stack(const int type, const int from, const int to)
{
	if (!resize_buffer((void *) &ustack, &usize, (u_ptr + 1) * sizeof(undo_t))) {
		show_strerror(0, errno);
		set_error_msg("Memory exhausted");
		if (ustack) {
			clear_undo_stack();
			free(ustack);
			ustack = 0;
			usize = u_ptr = 0;
			u_current_addr = u_addr_last = -1;
		}
		return 0;
	}
	ustack[u_ptr].type = type;
	ustack[u_ptr].tail = search_line_node((to >= 0) ? to : _current_addr);
	ustack[u_ptr].head = search_line_node((from >= 0) ? from : _current_addr);
	return ustack + u_ptr++;
}
