# User-space programs Makefile

CC = clang
LD = ld.lld

CFLAGS = -target x86_64-elf -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
         -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=small -Wall -Wextra \
         -I../include -O2

LDFLAGS = -nostdlib -T user.ld -z max-page-size=0x1000

USER_PROGS = hello.elf cat.elf init.elf ls.elf cp.elf

all: $(USER_PROGS)

libc.o: libc.c libc.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

hello.elf: hello.o
	$(LD) $(LDFLAGS) -o $@ $^

cat.elf: cat.o libc.o
	$(LD) $(LDFLAGS) -o $@ $^

init.elf: init.o libc.o
	$(LD) $(LDFLAGS) -o $@ $^

ls.elf: ls.o libc.o
	$(LD) $(LDFLAGS) -o $@ $^

cp.elf: cp.o libc.o
	$(LD) $(LDFLAGS) -o $@ $^

clean:
	rm -f *.o *.elf

.PHONY: all clean