#include "libc.h"

int main(int argc, char *argv[], char *envp[]) {
    // Initialize environment
    environ = envp;

    printf("ArchForge OS Init System v1.0\n");
    printf("Initializing system services...\n");

    // Mount filesystems could go here
    printf("Starting system shell...\n");

    // Spawn the shell
    char *shell_argv[] = { "shell", 0 };
    char *shell_envp[] = { "PATH=/bin", "USER=root", "HOME=/", 0 };

    int pid = fork();
    if (pid == 0) {
        // Child process: execute shell
        exec("/bin/shell", shell_argv, shell_envp);
        // If exec fails
        printf("init: failed to execute /bin/shell\n");
        exit(1);
    } else if (pid > 0) {
        // Parent process: wait for shell to exit
        int status;
        waitpid(pid, &status, 0);
        printf("init: shell exited with status %d. Halting system.\n", status);
        
        // In a real OS, we might respawn the shell or halt.
        // For now, we just halt.
        while (1) {
            yield();
        }
    } else {
        printf("init: fork failed\n");
        exit(1);
    }

    return 0;
}