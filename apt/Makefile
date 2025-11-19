PROGS = nslookup wget download
PROGS += apt
PROGS += apt-debug

default: $(PROGS)
INCLUDES = -include ./debug.h
CXXFLAGS = -g -fsanitize=address -Wall -O2
CXXFLAGS += -Wno-pointer-arith

CC = gcc
CXX = g++
CFLAGS = -g -fsanitize=address -Wall -O2

%.d: %.cpp
	$(CXX) $(INCLUDES) -E -MD -MF $@ $< >/dev/null

%.o: %.cpp %.d
	$(CXX) $(INCLUDES) $(CXXFLAGS) -c $< -o $@

%.d: %.c
	$(CC) -E -MD -MF $@ $< >/dev/null

%.o: %.c %.d
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	-rm -f *.o *.d *.a $(PROGS)

libnetutil.a: nio.o dns.o http.o debug.o
	ar rcs libnetutil.a nio.o dns.o http.o debug.o

nslookup: nio.o dns.o debug.o nslookup.o
	$(CXX) $(CXXFLAGS) nio.o debug.o dns.o nslookup.o -o nslookup
wget: url.o nio.o http.o debug.o wget.o dns.o
	$(CXX) $(CXXFLAGS) nio.o debug.o http.o wget.o url.o dns.o -lssl -o wget
download: nio.o http.o debug.o download.o dns.o
	$(CXX) $(CXXFLAGS) nio.o debug.o http.o dns.o download.o -lssl -o download

apt: apt.o aptlist.o debug.o
	$(CXX) $(CXXFLAGS) apt.o aptlist.o debug.o -o apt

apt-debug: apt
	@ln -sf $(shell realpath apt) apt-debug

include aptlist.d
include apt.d
include debug.d
include download.d
include dns.d
include nslookup.d
include nio.d
include http.d
include wget.d
include url.d

