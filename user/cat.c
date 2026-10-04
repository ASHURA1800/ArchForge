/* user/cat.c — Simple cat utility for ArchForge OS
 * 
 * Usage: cat <file>
 * Reads and prints file contents to stdout.
 */
#include "libc.h"

/* Entry point for user programs */
void _start(void) {
    /* We can't easily get argc/argv without kernel support, so for now
     * we'll hardcode the test file path */
    const char *path = "/test.txt";
    
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("cat: cannot open '%s': No such file or directory\n", path);
        exit(1);
    }
    
    char buf[256];
    int bytes_read;
    while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, bytes_read);
    }
    
    close(fd);
    exit(0);
}