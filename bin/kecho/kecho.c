#include <sos/sos.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
	for (int i = 1; i < argc; i++) {
		if (i > 1) kprint(" ");
		kprint(argv[i]);
	}

	kprint("\n");
	return 0;
}

