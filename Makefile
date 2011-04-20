CFLAGS=-Wall

all: 9mount 9umount 9bind

9mount: 9mount.c
9umount: 9umount.c
9bind: 9bind.c

%: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ -o $@

clean:
	rm -f 9mount 9umount 9bind
