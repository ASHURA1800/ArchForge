#include "libc.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: cp <source> <destination>\n");
        return 1;
    }

    FILE *src = fopen(argv[1], "r");
    if (!src) {
        printf("cp: cannot open '%s' for reading\n", argv[1]);
        return 1;
    }

    FILE *dst = fopen(argv[2], "w");
    if (!dst) {
        printf("cp: cannot open '%s' for writing\n", argv[2]);
        fclose(src);
        return 1;
    }

    char buf[512];
    size_t n;
    size_t total = 0;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        fwrite(buf, 1, n, dst);
        total += n;
    }

    printf("Copied %zu bytes from '%s' to '%s'\n", total, argv[1], argv[2]);

    fclose(src);
    fclose(dst);
    return 0;
}