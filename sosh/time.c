#include <stdio.h>
#include <sos/sos.h>
#include <string.h>

#include "commands.h"
#include "time.h"
#include "sosh.h"

int time(int argc, char **argv) {
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


