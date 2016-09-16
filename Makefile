CC=clang
CFLAGS=-Wall -c -g

BINS= libmythreads.o libmythreads.a


all: $(BINS)

libmythreads: libmythreads.c
	$(CC) $(CFLAGS) libmythreads.c

libmythreads.a: libmythreads.c
	ar -cvr libmythreads.a libmythreads.o

clean:
	rm $(BINS)

