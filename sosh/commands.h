#ifndef _SOSH_COMMANDS_H_
#define _SOSH_COMMANDS_H_

struct command {
	char *name;
	int (*command)(int argc, char **argv);
};

extern struct command sosh_commands[];

#endif // sosh/commands.h
