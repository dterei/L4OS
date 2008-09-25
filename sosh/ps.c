#include <sos/sos.h>
#include <stdio.h>

#include "ps.h"
#include "sosh.h"

int ps(int argc, char **argv) {
	process_t *process;
	int i, processes;

	process = malloc(MAX_PROCESSES * sizeof(*process));

	if (process == NULL) {
		printf("%s: out of memory\n", argv[0]);
		return 1;
	}

	processes = process_status(process, MAX_PROCESSES);

	printf("TID SIZE   STIME   CTIME COMMAND\n");

	for (i = 0; i < processes; i++) {
		printf("%3x %4x %7d %7d %s\n", process[i].pid, process[i].size,
				process[i].stime, process[i].ctime, process[i].command);
		usleep(1000 * 100);
	}

	free(process);

	return 0;
}

