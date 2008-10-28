#include <sos/sos.h>
#include <stdio.h>

#include <l4/thread.h>

int main(int argc, char **argv) {
	printf("My ID: %d\n", my_id());
	return 0;
}

