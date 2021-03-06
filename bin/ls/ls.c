#include <sos/sos.h>
#include <sos/globals.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static stat_t sbuf;

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

int main(int argc, char *argv[]) {
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

