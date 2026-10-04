#ifndef ARCHFORGE_SHELL_H
#define ARCHFORGE_SHELL_H

#include <stdint.h>
#include <stddef.h>

/* Initialize the shell */
void shell_init(void);

/* Run the shell main loop */
void shell_run(void);

/* Execute a command line */
int shell_execute(const char *cmdline);

/* Built-in command handler type */
typedef int (*shell_cmd_fn)(int argc, char *argv[]);

/* Register a built-in command */
int shell_register_command(const char *name, shell_cmd_fn fn, const char *help);

/* Get command by name */
shell_cmd_fn shell_find_command(const char *name);

/* Print usage for a command */
void shell_print_usage(const char *name);

#endif /* ARCHFORGE_SHELL_H */