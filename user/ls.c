#include "libc.h"

int main(int argc, char *argv[]) {
    const char *path = "/";
    if (argc > 1) {
        path = argv[1];
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        printf("ls: cannot open '%s'\n", path);
        return 1;
    }

    // For now, we just read and print the raw directory data 
    // since we don't have a full readdir wrapper yet.
    // In a real system, we'd use a syscall to read directory entries.
    char buf[512];
    size_t n;
    printf("Contents of %s:\n", path);
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        // Simple heuristic to print printable strings (filenames)
        for (size_t i = 0; i < n; i++) {
            if (buf[i] >= 32 && buf[i] < 127) {
                putchar(buf[i]);
            } else if (buf[i] == '\0' || buf[i] == '\n') {
                putchar('\n');
            }
        }
    }
    putchar('\n');

    fclose(f);
    return 0;
}

// Simple putchar for ls
int putchar(int c) {
    return write(1, &c, 1);
}