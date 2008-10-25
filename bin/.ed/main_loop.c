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
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ed.h"


enum Status { ERR = -2, EMOD = -3, FATAL = -4 };

static char def_filename[1024] = "";	/* default filename */
static char errmsg[80] = "";	/* error message buffer */
static char prompt_str[80] = "*";	/* command-line prompt */
static const char *ibufp;	/* pointer to ed command-line buffer */
static char *shcmd;		/* shell command buffer */
static int shcmdsz;		/* shell command buffer size */
static int shcmdi;		/* shell command buffer index */
static int first_addr, second_addr;
static char verbose = 0;	/* if set, print all error messages */
static char prompt_on = 0;	/* if set, show command-line prompt */


void set_def_filename(const char *s)
{
	strncpy(def_filename, s, sizeof(def_filename));
	def_filename[sizeof(def_filename) - 1] = 0;
}

void set_prompt(const char *s)
{
	prompt_on = 1;
	strncpy(prompt_str, s, sizeof(prompt_str));
	prompt_str[sizeof(prompt_str) - 1] = 0;
}

void set_verbose(void)
{
	verbose = 1;
}


static const line_t *mark[26];	/* line markers */
static int markno;		/* line marker count */

char mark_line_node(const line_t * lp, int c)
{
	c -= 'a';
	if (c < 0 || c >= 26) {
		set_error_msg("Invalid mark character");
		return 0;
	}
	if (!mark[c])
		++markno;
	mark[c] = lp;
	return 1;
}


void unmark_line_node(const line_t * lp)
{
	int i;

	for (i = 0; markno && i < 26; ++i)
		if (mark[i] == lp) {
			mark[i] = 0;
			--markno;
		}
}


/* return address of a marked line */
int get_marked_node_addr(int c)
{
	c -= 'a';
	if (c < 0 || c >= 26) {
		set_error_msg("Invalid mark character");
		return -1;
	}
	return get_line_node_addr(mark[c]);
}


/* read a shell command from stdin; return substitution status ( -1, 0, +1 ) */
int get_shell_command(void)
{
	static char *buf = 0;
	static int bsize = 0;
	const char *s;		/* substitution char pointer */
	int i = 0, len;

	if (restricted()) {
		set_error_msg("Shell access restricted");
		return -1;
	}
	s = ibufp = get_extended_line(ibufp, &len, 1);
	if (!s)
		return -1;
	if (!resize_buffer((void *) &buf, &bsize, len + 1))
		return -1;
	buf[i++] = '!';		/* prefix command w/ bang */
	while (*ibufp != '\n') {
		if (*ibufp == '!') {
			if (s != ibufp) {
				if (!resize_buffer((void *) &buf, &bsize, i + 1))
					return -1;
				buf[i++] = *ibufp++;
			} else if (!shcmd || (traditional() && !*(shcmd + 1))) {
				set_error_msg("No previous command");
				return -1;
			} else {
				if (!resize_buffer((void *) &buf, &bsize, i + shcmdi))
					return -1;
				for (s = shcmd + 1; s < shcmd + shcmdi;)
					buf[i++] = *s++;
				s = ibufp++;
			}
		} else if (*ibufp == '%') {
			if (!def_filename[0]) {
				set_error_msg("No current filename");
				return -1;
			}
			len = strlen(s = strip_escapes(def_filename));
			if (!resize_buffer((void *) &buf, &bsize, i + len))
				return -1;
			while (len--)
				buf[i++] = *s++;
			s = ibufp++;
		} else {
			if (!resize_buffer((void *) &buf, &bsize, i + 2))
				return -1;
			buf[i++] = *ibufp;
			if (*ibufp++ == '\\')
				buf[i++] = *ibufp++;
		}
	}
	if (!resize_buffer((void *) &shcmd, &shcmdsz, i + 1))
		return -1;
	memcpy(shcmd, buf, i);
	shcmdi = i;
	shcmd[i] = 0;
	return (*s == '!' || *s == '%');
}


/* return pointer to copy of filename in the command buffer */
const char *get_filename(void)
{
	static char *file = 0;
	static int filesz = 0;
	int size, n;

	if (*ibufp != '\n') {
		ibufp = skip_blanks(ibufp);
		if (*ibufp == '\n') {
			set_error_msg("Invalid filename");
			return 0;
		}
		ibufp = get_extended_line(ibufp, &size, 1);
		if (!ibufp)
			return 0;
		if (*ibufp == '!') {
			++ibufp;
			n = get_shell_command();
			if (n < 0)
				return 0;
			if (n)
				printf("%s\n", shcmd + 1);
			return shcmd;
		} else if (size > path_max(0)) {
			set_error_msg("Filename too long");
			return 0;
		}
	} else if (!traditional() && !def_filename[0]) {
		set_error_msg("No current filename");
		return 0;
	}
	if (!resize_buffer((void *) &file, &filesz, path_max(0) + 1))
		return 0;
	for (n = 0; *ibufp != '\n';)
		file[n++] = *ibufp++;
	file[n] = 0;
	return (is_valid_filename(file) ? file : 0);
}


void invalid_address(void)
{
	set_error_msg("Invalid address");
}


/* return the next line address in the command buffer */
int next_addr(int *addr_cnt)
{
	const char *hd = ibufp = skip_blanks(ibufp);
	int addr = current_addr();
	int first = 1;

	while (1) {
		int n;
		const unsigned char ch = *ibufp;

		if (isdigit(ch)) {
			if (!first) {
				invalid_address();
				return -2;
			};
			if (!parse_int(&addr, ibufp, &ibufp))
				return -2;
		} else
			switch (ch) {
				case '+':
				case '\t':
				case ' ':
				case '-':
					ibufp = skip_blanks(++ibufp);
					if (isdigit((unsigned char) *ibufp)) {
						if (!parse_int(&n, ibufp, &ibufp))
							return -2;
						addr += ((ch == '-') ? -n : n);
					} else if (ch == '+')
						++addr;
					else if (ch == '-')
						--addr;
					break;
				case '.':
				case '$':
					if (!first) {
						invalid_address();
						return -2;
					};
					++ibufp;
					addr = ((ch == '.') ? current_addr() : last_addr());
					break;
				case '/':
				case '?':
					if (!first) {
						invalid_address();
						return -2;
					};
					addr = get_matching_node_addr(&ibufp, ch == '/');
					if (addr < 0)
						return -2;
					if (ch == *ibufp)
						++ibufp;
					break;
				case '\'':
					if (!first) {
						invalid_address();
						return -2;
					};
					++ibufp;
					addr = get_marked_node_addr(*ibufp++);
					if (addr < 0)
						return -2;
					break;
				case '%':
				case ',':
				case ';':
					if (first) {
						++ibufp;
						++*addr_cnt;
						second_addr = ((ch == ';') ? current_addr() : 1);
						addr = last_addr();
						break;
					}		/* FALL THROUGH */
				default:
					if (ibufp == hd)
						return -1;	/* EOF */
					if (addr < 0 || addr > last_addr()) {
						invalid_address();
						return -2;
					}
					++*addr_cnt;
					return addr;
			}
		first = 0;
	}
}


/* get line addresses from the command buffer until an illegal address
	is seen. Return number of addresses read */
int extract_addr_range(void)
{
	int addr;
	int addr_cnt = 0;

	first_addr = second_addr = current_addr();
	while ((addr = next_addr(&addr_cnt)) >= 0) {
		first_addr = second_addr;
		second_addr = addr;
		if (*ibufp != ',' && *ibufp != ';')
			break;
		if (*ibufp++ == ';')
			set_current_addr(addr);
	}
	if (addr_cnt == 1 || second_addr != addr)
		first_addr = second_addr;
	return ((addr != -2) ? addr_cnt : -1);
}


/* get a valid address from the command buffer */
char get_third_addr(int *addr)
{
	int ol1 = first_addr;
	int ol2 = second_addr;

	int addr_cnt = extract_addr_range();

	if (addr_cnt < 0)
		return 0;
	if (traditional() && addr_cnt == 0) {
		set_error_msg("Destination expected");
		return 0;
	}
	if (second_addr < 0 || second_addr > last_addr()) {
		invalid_address();
		return 0;
	}
	*addr = second_addr;
	first_addr = ol1;
	second_addr = ol2;
	return 1;
}


/* return true if address range is valid */
char check_addr_range(const int n, const int m, const int addr_cnt)
{
	if (addr_cnt == 0) {
		first_addr = ((n >= 0) ? n : current_addr());
		second_addr = ((m >= 0) ? m : current_addr());
	}
	if (first_addr < 1 || first_addr > second_addr || second_addr > last_addr()) {
		invalid_address();
		return 0;
	}
	return 1;
}


/* verify the command suffix in the command buffer */
char get_command_suffix(int *gflagsp)
{
	while (1) {
		const char ch = *ibufp;

		if (ch == 'l')
			*gflagsp |= GLS;
		else if (ch == 'n')
			*gflagsp |= GNP;
		else if (ch == 'p')
			*gflagsp |= GPR;
		else
			break;
		++ibufp;
	}
	if (*ibufp++ != '\n') {
		set_error_msg("Invalid command suffix");
		return 0;
	}
	return 1;
}


char unexpected_address(const int addr_cnt)
{
	if (addr_cnt > 0) {
		set_error_msg("Unexpected address");
		return 1;
	}
	return 0;
}

char unexpected_command_suffix(const unsigned char ch)
{
	if (!isspace(ch)) {
		set_error_msg("Unexpected command suffix");
		return 1;
	}
	return 0;
}


char command_s(int *gflagsp, const int addr_cnt, const char isglobal)
{
	static int gflags = 0;
	static int snum = 0;
	enum Sflags {
		SGG = 0x01,		/* complement previous global substitute suffix */
		SGP = 0x02,		/* complement previous print suffix */
		SGR = 0x04,		/* use last regex instead of last pat */
		SGF = 0x08		/* repeat last substitution */
	} sflags = 0;

	do {
		if (isdigit((unsigned char) *ibufp)) {
			if (!parse_int(&snum, ibufp, &ibufp))
				return 0;
			sflags |= SGF;
			gflags &= ~GSG;	/* override GSG */
		} else
			switch (*ibufp) {
				case '\n':
					sflags |= SGF;
					break;
				case 'g':
					sflags |= SGG;
					++ibufp;
					break;
				case 'p':
					sflags |= SGP;
					++ibufp;
					break;
				case 'r':
					sflags |= SGR;
					++ibufp;
					break;
				default:
					if (sflags) {
						set_error_msg("Invalid command suffix");
						return 0;
					}
			}
	}
	while (sflags && *ibufp != '\n');
	if (sflags && !prev_pattern()) {
		set_error_msg("No previous substitution");
		return 0;
	}
	if (sflags & SGG)
		snum = 0;		/* override numeric arg */
	if (*ibufp != '\n' && *(ibufp + 1) == '\n') {
		set_error_msg("Invalid pattern delimiter");
		return 0;
	}
	if ((!sflags || (sflags & SGR)) && !new_compiled_pattern(&ibufp))
		return 0;
	if (!sflags && !extract_subst_tail(&ibufp, &gflags, &snum, isglobal))
		return 0;
	if (isglobal)
		gflags |= GLB;
	else
		gflags &= ~GLB;
	if (sflags & SGG)
		gflags ^= GSG;
	if (sflags & SGP) {
		gflags ^= GPR;
		gflags &= ~(GLS | GNP);
	}
	switch (*ibufp) {
		case 'l':
			gflags |= GLS;
			++ibufp;
			break;
		case 'n':
			gflags |= GNP;
			++ibufp;
			break;
		case 'p':
			gflags |= GPR;
			++ibufp;
			break;
	}
	if (!check_addr_range(-1, -1, addr_cnt))
		return 0;
	if (!get_command_suffix(gflagsp))
		return 0;
	if (!isglobal)
		clear_undo_stack();
	if (!search_and_replace(first_addr, second_addr, gflags, snum, isglobal))
		return 0;
	if ((gflags & (GPR | GLS | GNP)) &&
			!display_lines(current_addr(), current_addr(), gflags))
		return 0;
	return 1;
}


char exec_global(const char *ibufp2, int gflags, const char interact);

/* execute the next command in command buffer; return error status */
int exec_command(const char isglobal)
{
	const char *fnp;
	int gflags = 0;
	int status = 0;
	int addr, c, n;

	const int addr_cnt = extract_addr_range();

	if (addr_cnt < 0)
		return ERR;
	ibufp = skip_blanks(ibufp);
	c = *ibufp++;
	switch (c) {
		case 'a':
			if (!get_command_suffix(&gflags))
				return ERR;
			if (!isglobal)
				clear_undo_stack();
			if (!append_lines(ibufp, second_addr, isglobal))
				return ERR;
			ibufp = "";
			break;
		case 'c':
			if (first_addr == 0)
				first_addr = 1;
			if (second_addr == 0)
				second_addr = 1;
			if (!check_addr_range(-1, -1, addr_cnt) || !get_command_suffix(&gflags))
				return ERR;
			if (!isglobal)
				clear_undo_stack();
			if (!delete_lines(first_addr, second_addr, isglobal) ||
					!append_lines(ibufp, current_addr(), isglobal))
				return ERR;
			ibufp = "";
			break;
		case 'd':
			if (!check_addr_range(-1, -1, addr_cnt) || !get_command_suffix(&gflags))
				return ERR;
			if (!isglobal)
				clear_undo_stack();
			if (!delete_lines(first_addr, second_addr, isglobal))
				return ERR;
			inc_current_addr();
			break;
		case 'e':
			if (modified() && !scripted())
				return EMOD;	/* fall through */
		case 'E':
			if (unexpected_address(addr_cnt) || unexpected_command_suffix(*ibufp))
				return ERR;
			fnp = get_filename();
			if (!fnp || !get_command_suffix(&gflags) ||
					!delete_lines(1, last_addr(), isglobal))
				return ERR;
			if (!close_sbuf())
				return ERR;
			if (!open_sbuf())
				return FATAL;
			if (fnp[0] && fnp[0] != '!')
				set_def_filename(fnp);
			if (traditional() && !fnp[0] && !def_filename[0]) {
				set_error_msg("No current filename");
				return ERR;
			}
			if (read_file(fnp[0] ? fnp : def_filename, 0) < 0)
				return ERR;
			disable_undo();
			set_modified(0);
			break;
		case 'f':
			if (unexpected_address(addr_cnt) || unexpected_command_suffix(*ibufp))
				return ERR;
			fnp = get_filename();
			if (!fnp)
				return ERR;
			if (fnp[0] == '!') {
				set_error_msg("Invalid redirection");
				return ERR;
			}
			if (!get_command_suffix(&gflags))
				return ERR;
			if (fnp[0])
				set_def_filename(fnp);
			printf("%s\n", strip_escapes(def_filename));
			break;
		case 'g':
		case 'v':
		case 'G':
		case 'V':
			if (isglobal) {
				set_error_msg("Cannot nest global commands");
				return ERR;
			}
			n = (c == 'g' || c == 'G');	/* mark matching lines */
			if (!check_addr_range(1, last_addr(), addr_cnt) ||
					!build_active_list(&ibufp, first_addr, second_addr, n))
				return ERR;
			n = (c == 'G' || c == 'V');	/* interact */
			if ((n && !get_command_suffix(&gflags)) ||
					!exec_global(ibufp, gflags, n))
				return ERR;
			break;
		case 'h':
		case 'H':
			if (unexpected_address(addr_cnt) || !get_command_suffix(&gflags))
				return ERR;
			if (c == 'H')
				verbose = !verbose;
			if ((c == 'h' || verbose) && errmsg[0])
				fprintf(stderr, "%s\n", errmsg);
			break;
		case 'i':
			if (second_addr == 0)
				second_addr = 1;
			if (!get_command_suffix(&gflags))
				return ERR;
			if (!isglobal)
				clear_undo_stack();
			if (!append_lines(ibufp, second_addr - 1, isglobal))
				return ERR;
			ibufp = "";
			break;
		case 'j':
			if (!check_addr_range(-1, current_addr() + 1, addr_cnt) ||
					!get_command_suffix(&gflags))
				return ERR;
			if (!isglobal)
				clear_undo_stack();
			if (first_addr != second_addr &&
					!join_lines(first_addr, second_addr, isglobal))
				return ERR;
			break;
		case 'k':
			n = *ibufp++;
			if (second_addr == 0) {
				invalid_address();
				return ERR;
			}
			if (!get_command_suffix(&gflags) ||
					!mark_line_node(search_line_node(second_addr), n))
				return ERR;
			break;
		case 'l':
		case 'n':
		case 'p':
			if (c == 'l')
				n = GLS;
			else if (c == 'n')
				n = GNP;
			else
				n = GPR;
			if (!check_addr_range(-1, -1, addr_cnt) ||
					!get_command_suffix(&gflags) ||
					!display_lines(first_addr, second_addr, gflags | n))
				return ERR;
			gflags = 0;
			break;
		case 'm':
			if (!check_addr_range(-1, -1, addr_cnt) || !get_third_addr(&addr))
				return ERR;
			if (addr >= first_addr && addr < second_addr) {
				set_error_msg("Invalid destination");
				return ERR;
			}
			if (!get_command_suffix(&gflags))
				return ERR;
			if (!isglobal)
				clear_undo_stack();
			if (!move_lines(first_addr, second_addr, addr, isglobal))
				return ERR;
			break;
		case 'P':
		case 'q':
		case 'Q':
			if (unexpected_address(addr_cnt) || !get_command_suffix(&gflags))
				return ERR;
			if (c == 'P')
				prompt_on = !prompt_on;
			else
				status = ((modified() && !scripted() && c == 'q') ? EMOD : -1);
			break;
		case 'r':
			if (unexpected_command_suffix(*ibufp))
				return ERR;
			if (addr_cnt == 0)
				second_addr = last_addr();
			fnp = get_filename();
			if (!fnp || !get_command_suffix(&gflags))
				return ERR;
			if (!isglobal)
				clear_undo_stack();
			if (!def_filename[0] && fnp[0] != '!')
				set_def_filename(fnp);
			if (traditional() && !fnp[0] && !def_filename[0]) {
				set_error_msg("No current filename");
				return ERR;
			}
			addr = read_file(fnp[0] ? fnp : def_filename, second_addr);
			if (addr < 0)
				return ERR;
			if (addr && addr != last_addr())
				set_modified(1);
			break;
		case 's':
			if (!command_s(&gflags, addr_cnt, isglobal))
				return ERR;
			break;
		case 't':
			if (!check_addr_range(-1, -1, addr_cnt) ||
					!get_third_addr(&addr) || !get_command_suffix(&gflags))
				return ERR;
			if (!isglobal)
				clear_undo_stack();
			if (!copy_lines(first_addr, second_addr, addr))
				return ERR;
			break;
		case 'u':
			if (unexpected_address(addr_cnt) ||
					!get_command_suffix(&gflags) || !pop_undo_stack(isglobal))
				return ERR;
			break;
		case 'w':
		case 'W':
			n = *ibufp;
			if (n == 'q' || n == 'Q') {
				status = -1;
				++ibufp;
			}
			if (unexpected_command_suffix(*ibufp))
				return ERR;
			fnp = get_filename();
			if (!fnp)
				return ERR;
			if (addr_cnt == 0 && !last_addr())
				first_addr = second_addr = 0;
			else if (!check_addr_range(1, last_addr(), addr_cnt))
				return ERR;
			if (!get_command_suffix(&gflags))
				return ERR;
			if (!def_filename[0] && fnp[0] != '!')
				set_def_filename(fnp);
			if (traditional() && !fnp[0] && !def_filename[0]) {
				set_error_msg("No current filename");
				return ERR;
			}
			addr = write_file(fnp[0] ? fnp : def_filename,
					(c == 'W') ? "a" : "w", first_addr, second_addr);
			if (addr < 0)
				return ERR;
			if (addr == last_addr())
				set_modified(0);
			else if (modified() && !scripted() && n == 'q')
				status = EMOD;
			break;
		case 'x':
			if (second_addr < 0 || last_addr() < second_addr) {
				invalid_address();
				return ERR;
			}
			if (!get_command_suffix(&gflags))
				return ERR;
			if (!isglobal)
				clear_undo_stack();
			if (!put_lines(second_addr))
				return ERR;
			break;
		case 'y':
			if (!check_addr_range(-1, -1, addr_cnt) ||
					!get_command_suffix(&gflags) ||
					!yank_lines(first_addr, second_addr))
				return ERR;
			break;
		case 'z':
			first_addr = 1;
			if (!check_addr_range(first_addr, current_addr() +
						(traditional() || !isglobal), addr_cnt))
				return ERR;
			if (*ibufp > '0' && *ibufp <= '9') {
				if (parse_int(&n, ibufp, &ibufp))
					set_window_lines(n);
				else
					return ERR;
			}
			if (!get_command_suffix(&gflags) ||
					!display_lines(second_addr,
						min(last_addr(), second_addr + window_lines()),
						gflags))
				return ERR;
			gflags = 0;
			break;
		case '=':
			if (!get_command_suffix(&gflags))
				return ERR;
			printf("%d\n", addr_cnt ? second_addr : last_addr());
			break;
		case '!':
			if (unexpected_address(addr_cnt))
				return ERR;
			n = get_shell_command();
			if (n < 0 || !get_command_suffix(&gflags))
				return ERR;
			if (n)
				printf("%s\n", shcmd + 1);
			system(shcmd + 1);
			if (!scripted())
				printf("!\n");
			break;
		case '\n':
			first_addr = 1;
			if (!check_addr_range(first_addr, current_addr() +
						(traditional() || !isglobal), addr_cnt) ||
					!display_lines(second_addr, second_addr, 0))
				return ERR;
			break;
		case '#':
			while (*ibufp++ != '\n');
			break;
		default:
			set_error_msg("Unknown command");
			return ERR;
	}
	if (!status && gflags &&
			!display_lines(current_addr(), current_addr(), gflags))
		status = ERR;
	return status;
}


/* apply command list in the command buffer to the active lines in a
	range; return false if error */
char exec_global(const char *ibufp2, int gflags, const char interact)
{
	static char *ocmd = 0;
	static int ocmdsz = 0;
	const line_t *lp = 0;
	const char *cmd = 0;

	if (!interact) {
		if (traditional() && !strcmp(ibufp2, "\n"))
			cmd = "p\n";	/* null cmd-list == `p' */
		else if (!(cmd = get_extended_line(ibufp2, 0, 0)))
			return 0;
	}
	clear_undo_stack();
	while ((lp = next_active_node())) {
		set_current_addr(get_line_node_addr(lp));
		if (current_addr() < 0)
			return 0;
		if (interact) {
			/* print current_addr; get a command in global syntax */
			int len;

			if (!display_lines(current_addr(), current_addr(), gflags))
				return 0;
			do {
				ibufp2 = get_tty_line(&len);
			}
			while (ibufp2 && len > 0 && ibufp2[len - 1] != '\n');
			if (!ibufp2)
				return 0;
			if (len == 0) {
				set_error_msg("Unexpected end-of-file");
				return 0;
			}
			if (len == 1 && !strcmp(ibufp2, "\n"))
				continue;
			if (len == 2 && !strcmp(ibufp2, "&\n")) {
				if (!cmd) {
					set_error_msg("No previous command");
					return 0;
				}
			} else if (!(cmd = get_extended_line(ibufp2, &len, 0)))
				return 0;
			else {
				if (!resize_buffer((void *) &ocmd, &ocmdsz, len + 1))
					return 0;
				memcpy(ocmd, cmd, len + 1);
				cmd = ocmd;
			}
		}
		ibufp = cmd;
		while (*ibufp)
			if (exec_command(1) < 0)
				return 0;
	}
	return 1;
}


int main_loop(const char loose)
{
	extern jmp_buf jmp_state;
	const char *ibufp2;
	volatile int err_status = 0;	/* program exit status */
	volatile int linenum = 0;	/* script line number */
	int len, status;

	disable_interrupts();
	set_signals();
	status = setjmp(jmp_state);
	if (!status)
		enable_interrupts();
	else {
		fputs("\n?\n", stderr);
		set_error_msg("Interrupt");
	}

	while (1) {
		if (status < 0 && verbose)
			fprintf(stderr, "%s\n", errmsg);
		if (prompt_on) {
			printf("%s", prompt_str);
			fflush(stdout);
		}
		ibufp2 = get_tty_line(&len);
		if (!ibufp2)
			return err_status;	/* { status = ERR; continue; } */
		if (!len) {
			if (!modified() || scripted())
				return err_status;
			fputs("?\n", stderr);
			set_error_msg("Warning: file modified");
			if (is_regular_file(0)) {
				if (verbose)
					fprintf(stderr, "script, line %d: %s\n", linenum, errmsg);
				return 2;
			}
			set_modified(0);
			status = EMOD;
			continue;
		} else if (ibufp2[len - 1] != '\n') {	/* discard line */
			set_error_msg("Unexpected end-of-file");
			status = ERR;
			continue;
		} else
			++linenum;
		ibufp = ibufp2;
		status = exec_command(0);
		if (status == 0)
			continue;
		if (status == -1)
			return err_status;
		if (!loose)
			err_status = 1;
		if (status == EMOD) {
			set_modified(0);
			fputs("?\n", stderr);	/* give warning */
			set_error_msg("Warning: file modified");
			if (is_regular_file(0)) {
				if (verbose)
					fprintf(stderr, "script, line %d: %s\n", linenum, errmsg);
				return 1;
			}
		} else if (status == FATAL) {
			if (verbose) {
				if (is_regular_file(0))
					fprintf(stderr, "script, line %d: %s\n", linenum, errmsg);
				else
					fprintf(stderr, "%s\n", errmsg);
			}
			return 1;
		} else {
			fputs("?\n", stderr);	/* give warning */
			if (is_regular_file(0)) {
				if (verbose)
					fprintf(stderr, "script, line %d: %s\n", linenum, errmsg);
				return 1;
			}
		}
	}
}


void set_error_msg(const char *msg)
{
	if (!msg)
		msg = "";
	strncpy(errmsg, msg, sizeof(errmsg));
	errmsg[sizeof(errmsg) - 1] = 0;
}
