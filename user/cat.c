#include "libc.h"

void _start(void) {
    printf("Cat utility starting...\n");
    
    const char *filename = "/test.txt";
    int fd = open(filename, 0);
    if (fd < 0) {
        printf("Failed to open %s (fd=%d)\n", filename, fd);
        exit(1);
    }
    
    printf("Opened %s, fd=%d\n", filename, fd);
    
    char buf[256];
    int bytes_read;
    while ((bytes_read = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes_read] = '\0';
        write(1, buf, bytes_read);
    }
    
    printf("\n--- End of file ---\n");
    close(fd);
    exit(0);
}
