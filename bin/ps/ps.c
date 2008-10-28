#include <sos/sos.h>
#include <sos/globals.h>
#include <stdio.h>
#include <string.h>

#define MAX_PROCESSES 256

int main(int argc, char *argv[]) {
	process_t *process;
	int i, processes;
	//char buf[BUFSIZ];
	//fildes_t console;

	process = malloc(MAX_PROCESSES * sizeof(*process));

	if (process == NULL) {
		printf("%s: out of memory\n", argv[0]);
		return 1;
	}

	processes = process_status(process, MAX_PROCESSES);

	//console = open("console", FM_WRITE);
	printf("%3s %8s %6s %10s %6s %-10s\n", "TID", "STATE", "SIZE", "STIME", "CTIME", "COMMAND");
	//write(console, buf, strlen(buf));

	for (i = 0; i < processes; i++) {
		printf("%3d %8s %6d %10d %6d %-10s\n", process[i].pid, process_state_show(process[i].state),
				process[i].size, process[i].stime, process[i].ctime, process[i].command);
		//write(console, buf, strlen(buf));
	}

	free(process);
	//close(console);

	return 0;
}

