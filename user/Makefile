# ArchForge OS User Programs Makefile

CC = x86_64-elf-gcc
LD = x86_64-elf-ld
CFLAGS = -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -mno-sse -O2 -Wall -Wextra
LDFLAGS = -T user.ld -nostdlib

.PHONY: all clean

all: hello.elf cat.elf

hello.elf: hello.o
	$(LD) $(LDFLAGS) -o $@ $^

hello.o: hello.c
	$(CC) $(CFLAGS) -c -o $@ $<

cat.elf: cat.o libc.o
	$(LD) $(LDFLAGS) -o $@ $^

cat.o: cat.c libc.h
	$(CC) $(CFLAGS) -c -o $@ $<

libc.o: libc.c libc.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.elf