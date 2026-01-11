PROGS = 
PROGS += apt
PROGS += apt-debug

default: $(PROGS)
all: $(PROGS)
DEFS = -DCONFIG_HAS_SSL
INCLUDES = -include ./debug.h
CXXFLAGS = -g -fsanitize=address -Wall -O2
CXXFLAGS += -Wno-pointer-arith
CXXFLAGS += -std=c++17
CXXFLAGS += -I./libhttpd

CC = gcc
CXX = g++
CFLAGS = -g -fsanitize=address -Wall -O2

%.o: %.cpp
	$(CXX) $(DEFS) $(INCLUDES) $(CXXFLAGS) -MD -c $< -o $@

%.o: %.c
	$(CC) $(DEFS) $(CFLAGS) -MD -c $< -o $@

.PHONY: clean
clean:
	-rm -f *.o *.d *.a $(PROGS)
	-make -C libhttpd clean

apt-debug: apt
	@ln -sf $(shell realpath apt) apt-debug

-include *.d

SRCS := $(wildcard *.cpp)
OBJS := $(SRCS:.cpp=.o)
SRCS += url.c
OBJS += url.o

libhttpd/libhttpd.a:
	$(MAKE) -C libhttpd

apt: $(OBJS) libhttpd/libhttpd.a
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread -ldl -lssl -lcrypto -L./libhttpd -lhttpd

debug:
	echo $(OBJS)
