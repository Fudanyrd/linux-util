all: a.out libmalloc.so

CC = gcc
CXX = gcc
CFLAGS = -O2 -g -Wno-unused-result
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -std=c++17

%.o: %.cc Makefile
	$(CC) $(CXXFLAGS) -c -o $@ $<

%.o: %.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $<

a.out: malloc.o mem.o node.o
	$(CC) malloc.o mem.o node.o

libmalloc.so: malloc.c
	$(CC) $(CFLAGS) -fpic -shared -o libmalloc.so malloc.c

.PHONY: clean
clean:
	-rm -f *.o *.out *.so

