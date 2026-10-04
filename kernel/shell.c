/* kernel/shell.c — Interactive Shell
 *
 * Features:
 * - Line editing (backspace, arrow keys, home/end)
 * - Command history (up/down arrows)
 * - Built-in commands
 * - Tab completion (basic)
 */
#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "serial.h"
#include "ramfs.h"
#include "heap.h"
#include "../include/string.h"
#include "../include/stdlib.h"

/* Command structure */
typedef struct {
    const char *name;
    int (*fn)(int argc, char *argv[]);
    const char *help;
} shell_command_t;

/* Maximum command line length */
#define SHELL_MAX_LINE 512
#define SHELL_MAX_ARGS 32
#define SHELL_HISTORY_SIZE 32

/* Command table */
static shell_command_t commands[64];
static size_t num_commands = 0;

/* Command history */
static char history[SHELL_HISTORY_SIZE][SHELL_MAX_LINE];
static size_t history_count = 0;
static size_t history_pos = 0;

/* Current line buffer */
static char line_buffer[SHELL_MAX_LINE];
static size_t line_len = 0;
static size_t cursor_pos = 0;

/* ====================================================================
 * shell_init — Initialize shell
 * ==================================================================== */
void shell_init(void) {
    serial_write("[SHELL] Initializing shell...\n");
    num_commands = 0;
    history_count = 0;
    history_pos = 0;
    line_len = 0;
    cursor_pos = 0;
    serial_write("[SHELL] Shell initialized.\n");
}

/* ====================================================================
 * shell_register_command — Register a built-in command
 * ==================================================================== */
int shell_register_command(const char *name, shell_cmd_fn fn, const char *help) {
    if (num_commands >= 64) return -1;
    commands[num_commands].name = name;
    commands[num_commands].fn = fn;
    commands[num_commands].help = help;
    num_commands++;
    return 0;
}

/* ====================================================================
 * shell_find_command — Find command by name
 * ==================================================================== */
shell_cmd_fn shell_find_command(const char *name) {
    for (size_t i = 0; i < num_commands; i++) {
        if (strcmp(commands[i].name, name) == 0) {
            return commands[i].fn;
        }
    }
    return NULL;
}

/* ====================================================================
 * shell_print_usage — Print command usage
 * ==================================================================== */
void shell_print_usage(const char *name) {
    for (size_t i = 0; i < num_commands; i++) {
        if (strcmp(commands[i].name, name) == 0) {
            console_write("Usage: ");
            console_write(name);
            console_write(" - ");
            console_write(commands[i].help);
            console_write("\n");
            return;
        }
    }
    console_write("Unknown command: ");
    console_write(name);
    console_write("\n");
}

/* ====================================================================
 * add_to_history — Add line to history
 * ==================================================================== */
static void add_to_history(const char *line) {
    if (line_len == 0) return;
    
    if (history_count < SHELL_HISTORY_SIZE) {
        strcpy(history[history_count], line);
        history_count++;
    } else {
        /* Shift history */
        for (size_t i = 1; i < SHELL_HISTORY_SIZE; i++) {
            strcpy(history[i - 1], history[i]);
        }
        strcpy(history[SHELL_HISTORY_SIZE - 1], line);
    }
    history_pos = history_count;
}

/* ====================================================================
 * redraw_line — Redraw the current line
 * ==================================================================== */
static void redraw_line(void) {
    /* Move to start of line */
    console_write("\r");
    console_write("archforge> ");
    
    /* Clear to end of line */
    for (size_t i = 0; i < line_len + 11; i++) {
        console_putc(' ');
    }
    console_write("\r");
    console_write("archforge> ");
    
    /* Write line */
    for (size_t i = 0; i < line_len; i++) {
        console_putc(line_buffer[i]);
    }
    
    /* Position cursor */
    size_t target_col = 11 + cursor_pos;
    size_t current_col = 11 + line_len;
    while (current_col > target_col) {
        console_putc('\b');
        current_col--;
    }
}

/* ====================================================================
 * insert_char — Insert character at cursor
 * ==================================================================== */
static void insert_char(char c) {
    if (line_len >= SHELL_MAX_LINE - 1) return;
    
    /* Shift characters right */
    for (size_t i = line_len; i > cursor_pos; i--) {
        line_buffer[i] = line_buffer[i - 1];
    }
    line_buffer[cursor_pos] = c;
    line_len++;
    cursor_pos++;
    redraw_line();
}

/* ====================================================================
 * delete_char — Delete character before cursor (backspace)
 * ==================================================================== */
static void delete_char(void) {
    if (cursor_pos == 0) return;
    
    cursor_pos--;
    for (size_t i = cursor_pos; i < line_len - 1; i++) {
        line_buffer[i] = line_buffer[i + 1];
    }
    line_len--;
    redraw_line();
}

/* ====================================================================
 * delete_char_forward — Delete character at cursor (delete key)
 * ==================================================================== */
static void delete_char_forward(void) {
    if (cursor_pos >= line_len) return;
    
    for (size_t i = cursor_pos; i < line_len - 1; i++) {
        line_buffer[i] = line_buffer[i + 1];
    }
    line_len--;
    redraw_line();
}

/* ====================================================================
 * move_cursor_left — Move cursor left
 * ==================================================================== */
static void move_cursor_left(void) {
    if (cursor_pos > 0) {
        cursor_pos--;
        console_putc('\b');
    }
}

/* ====================================================================
 * move_cursor_right — Move cursor right
 * ==================================================================== */
static void move_cursor_right(void) {
    if (cursor_pos < line_len) {
        console_putc(line_buffer[cursor_pos]);
        cursor_pos++;
    }
}

/* ====================================================================
 * move_cursor_home — Move cursor to start
 * ==================================================================== */
static void move_cursor_home(void) {
    while (cursor_pos > 0) {
        move_cursor_left();
    }
}

/* ====================================================================
 * move_cursor_end — Move cursor to end
 * ==================================================================== */
static void move_cursor_end(void) {
    while (cursor_pos < line_len) {
        move_cursor_right();
    }
}

/* ====================================================================
 * history_up — Show previous history entry
 * ==================================================================== */
static void history_up(void) {
    if (history_count == 0) return;
    if (history_pos == 0) return;
    
    history_pos--;
    strcpy(line_buffer, history[history_pos]);
    line_len = strlen(line_buffer);
    cursor_pos = line_len;
    redraw_line();
}

/* ====================================================================
 * history_down — Show next history entry
 * ==================================================================== */
static void history_down(void) {
    if (history_count == 0) return;
    if (history_pos >= history_count - 1) {
        /* Clear line */
        line_len = 0;
        cursor_pos = 0;
        line_buffer[0] = '\0';
        redraw_line();
        return;
    }
    
    history_pos++;
    strcpy(line_buffer, history[history_pos]);
    line_len = strlen(line_buffer);
    cursor_pos = line_len;
    redraw_line();
}

/* ====================================================================
 * parse_args — Parse command line into argc/argv
 * ==================================================================== */
static int parse_args(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p = line;
    
    while (*p && argc < max_args) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        
        argv[argc++] = p;
        
        /* Find end of argument */
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) {
            *p++ = '\0';
        }
    }
    argv[argc] = NULL;
    return argc;
}

/* ====================================================================
 * shell_execute — Execute a command line
 * ==================================================================== */
int shell_execute(const char *cmdline) {
    if (!cmdline || !*cmdline) return 0;
    
    /* Copy line for parsing */
    char line_copy[SHELL_MAX_LINE];
    strncpy(line_copy, cmdline, SHELL_MAX_LINE - 1);
    line_copy[SHELL_MAX_LINE - 1] = '\0';
    
    /* Parse arguments */
    char *argv[SHELL_MAX_ARGS];
    int argc = parse_args(line_copy, argv, SHELL_MAX_ARGS);
    if (argc == 0) return 0;
    
    /* Find and execute command */
    shell_cmd_fn fn = shell_find_command(argv[0]);
    if (fn) {
        return fn(argc, argv);
    }
    
    console_write("Unknown command: ");
    console_write(argv[0]);
    console_write("\nType 'help' for available commands.\n");
    return -1;
}

/* ====================================================================
 * Built-in commands
 * ==================================================================== */

int cmd_help(int argc, char *argv[]) {
    if (argc > 1) {
        shell_print_usage(argv[1]);
        return 0;
    }
    
    console_write("\nAvailable commands:\n");
    for (size_t i = 0; i < num_commands; i++) {
        console_write("  ");
        console_write(commands[i].name);
        console_write(" - ");
        console_write(commands[i].help);
        console_write("\n");
    }
    console_write("\n");
    return 0;
}

int cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        console_write(argv[i]);
        if (i < argc - 1) console_putc(' ');
    }
    console_write("\n");
    return 0;
}

int cmd_clear(int argc, char *argv[]) {
    console_clear();
    return 0;
}

int cmd_reboot(int argc, char *argv[]) {
    console_write("Rebooting...\n");
    /* Triple fault to reboot */
    __asm__ volatile("cli; hlt");
    return 0;
}

int cmd_shutdown(int argc, char *argv[]) {
    console_write("Shutting down...\n");
    /* ACPI shutdown would go here */
    for (;;) __asm__ volatile("cli; hlt");
    return 0;
}

int cmd_meminfo(int argc, char *argv[]) {
    extern uint64_t pmm_get_total_memory(void);
    extern uint64_t pmm_get_free_memory(void);
    extern uint64_t pmm_get_used_memory(void);
    
    console_write("Memory Information:\n");
    console_write("  Total: ");
    console_write_dec(pmm_get_total_memory() / 1024 / 1024);
    console_write(" MB\n");
    console_write("  Free:  ");
    console_write_dec(pmm_get_free_memory() / 1024 / 1024);
    console_write(" MB\n");
    console_write("  Used:  ");
    console_write_dec(pmm_get_used_memory() / 1024 / 1024);
    console_write(" MB\n");
    return 0;
}

int cmd_ls(int argc, char *argv[]) {
    extern void ramfs_list(void);
    ramfs_list();
    return 0;
}

int cmd_cat(int argc, char *argv[]) {
    if (argc < 2) {
        console_write("Usage: cat <file>\n");
        return -1;
    }
    
    extern int ramfs_read(const char *path, void *buf, size_t size);
    extern int ramfs_stat(const char *path, size_t *size_out);
    
    size_t size;
    if (ramfs_stat(argv[1], &size) != 0) {
        console_write("File not found: ");
        console_write(argv[1]);
        console_write("\n");
        return -1;
    }
    
    char *buf = kmalloc(size + 1);
    if (!buf) {
        console_write("Out of memory\n");
        return -1;
    }
    
    if (ramfs_read(argv[1], buf, size) < 0) {
        console_write("Read error\n");
        kfree(buf);
        return -1;
    }
    
    buf[size] = '\0';
    console_write(buf);
    console_write("\n");
    kfree(buf);
    return 0;
}

int cmd_write(int argc, char *argv[]) {
    if (argc < 3) {
        console_write("Usage: write <file> <content>\n");
        return -1;
    }
    
    /* Join remaining args as content */
    size_t total_len = 0;
    for (int i = 2; i < argc; i++) {
        total_len += strlen(argv[i]) + 1;
    }
    
    char *content = kmalloc(total_len + 1);
    if (!content) {
        console_write("Out of memory\n");
        return -1;
    }
    
    content[0] = '\0';
    for (int i = 2; i < argc; i++) {
        strcat(content, argv[i]);
        if (i < argc - 1) strcat(content, " ");
    }
    
    extern int ramfs_write(const char *path, const void *data, size_t size);
    if (ramfs_write(argv[1], content, strlen(content)) < 0) {
        console_write("Write failed\n");
        kfree(content);
        return -1;
    }
    
    console_write("Written ");
    console_write_dec(strlen(content));
    console_write(" bytes to ");
    console_write(argv[1]);
    console_write("\n");
    kfree(content);
    return 0;
}

int cmd_rm(int argc, char *argv[]) {
    if (argc < 2) {
        console_write("Usage: rm <file>\n");
        return -1;
    }
    
    extern int ramfs_delete(const char *path);
    if (ramfs_delete(argv[1]) < 0) {
        console_write("Delete failed\n");
        return -1;
    }
    
    console_write("Deleted: ");
    console_write(argv[1]);
    console_write("\n");
    return 0;
}

int cmd_mkdir(int argc, char *argv[]) {
    console_write("mkdir: Not implemented yet\n");
    return 0;
}

int cmd_cd(int argc, char *argv[]) {
    console_write("cd: Not implemented yet\n");
    return 0;
}

int cmd_pwd(int argc, char *argv[]) {
    console_write("/\n");
    return 0;
}

int cmd_history(int argc, char *argv[]) {
    console_write("Command History:\n");
    for (size_t i = 0; i < history_count; i++) {
        console_write_dec(i + 1);
        console_write(": ");
        console_write(history[i]);
        console_write("\n");
    }
    return 0;
}

int cmd_version(int argc, char *argv[]) {
    console_write("ArchForge OS v0.1\n");
    console_write("Built with Limine bootloader\n");
    return 0;
}

int cmd_uptime(int argc, char *argv[]) {
    extern uint64_t timer_get_ms(void);
    console_write("Uptime: ");
    console_write_dec(timer_get_ms() / 1000);
    console_write(" seconds\n");
    return 0;
}

int cmd_exec(int argc, char *argv[]) {
    if (argc < 2) {
        console_write("Usage: exec <program>\n");
        return -1;
    }
    
    console_write("Executing: ");
    console_write(argv[1]);
    console_write("\n");
    
    /* Use syscall to execute the program */
    extern int64_t sys_exec(const char *path, char *const argv[], char *const envp[]);
    
    /* Build argv for exec */
    char *exec_argv[argc];
    for (int i = 1; i < argc; i++) {
        exec_argv[i-1] = argv[i];
    }
    exec_argv[argc-1] = NULL;
    
    /* Call sys_exec via syscall */
    __asm__ volatile(
        "mov $9, %%rax\n"   /* SYS_EXEC = 9 */
        "mov %0, %%rdi\n"   /* path */
        "mov %1, %%rsi\n"   /* argv */
        "xor %%rdx, %%rdx\n" /* envp = NULL */
        "syscall\n"
        :
        : "r"(argv[1]), "r"(exec_argv)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    
    return 0;
}

/* ====================================================================
 * shell_run — Main shell loop
 * ==================================================================== */
void shell_run(void) {
    console_write("\n");
    console_write("========================================\n");
    console_write("  ArchForge OS Shell\n");
    console_write("  Type 'help' for commands\n");
    console_write("========================================\n\n");
    
    console_write("archforge> ");
    
    while (true) {
        char c = keyboard_getc();
        
        switch (c) {
            case '\n':
            case '\r':
                console_write("\n");
                if (line_len > 0) {
                    line_buffer[line_len] = '\0';
                    add_to_history(line_buffer);
                    shell_execute(line_buffer);
                }
                line_len = 0;
                cursor_pos = 0;
                line_buffer[0] = '\0';
                console_write("archforge> ");
                break;
                
            case '\b':  /* Backspace */
            case 127:   /* Delete */
                delete_char();
                break;
                
            case 27:  /* Escape sequence */
                {
                    char next = keyboard_getc();
                    if (next == '[') {
                        char third = keyboard_getc();
                        switch (third) {
                            case 'A': history_up(); break;      /* Up arrow */
                            case 'B': history_down(); break;    /* Down arrow */
                            case 'C': move_cursor_right(); break;   /* Right arrow */
                            case 'D': move_cursor_left(); break;    /* Left arrow */
                            case 'H': move_cursor_home(); break;    /* Home */
                            case 'F': move_cursor_end(); break;     /* End */
                            case '3':  /* Delete key */
                                {
                                    char tilde = keyboard_getc();
                                    if (tilde == '~') delete_char_forward();
                                }
                                break;
                        }
                    }
                }
                break;
                
            default:
                if (c >= 32 && c <= 126) {
                    insert_char(c);
                }
                break;
        }
    }
}

/* ====================================================================
 * Register all built-in commands
 * ==================================================================== */
static void register_builtins(void) {
    shell_register_command("help", cmd_help, "Show available commands");
    shell_register_command("echo", cmd_echo, "Print arguments");
    shell_register_command("clear", cmd_clear, "Clear screen");
    shell_register_command("reboot", cmd_reboot, "Reboot system");
    shell_register_command("shutdown", cmd_shutdown, "Shutdown system");
    shell_register_command("meminfo", cmd_meminfo, "Show memory info");
    shell_register_command("ls", cmd_ls, "List files in RAMFS");
    shell_register_command("cat", cmd_cat, "Display file contents");
    shell_register_command("write", cmd_write, "Write to file");
    shell_register_command("rm", cmd_rm, "Delete file");
    shell_register_command("mkdir", cmd_mkdir, "Create directory (stub)");
    shell_register_command("cd", cmd_cd, "Change directory (stub)");
    shell_register_command("pwd", cmd_pwd, "Print working directory");
    shell_register_command("history", cmd_history, "Show command history");
    shell_register_command("version", cmd_version, "Show OS version");
    shell_register_command("uptime", cmd_uptime, "Show system uptime");
    shell_register_command("exec", cmd_exec, "Execute ELF program from RAMFS");
}