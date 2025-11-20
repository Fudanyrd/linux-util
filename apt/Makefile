PROGS = nslookup wget download
PROGS += apt
PROGS += apt-debug

default: $(PROGS)
all: $(PROGS)
DEFS = 
INCLUDES = -include ./debug.h
CXXFLAGS = -g -fsanitize=address -Wall -O2
CXXFLAGS += -Wno-pointer-arith

CC = gcc
CXX = g++
CFLAGS = -g -fsanitize=address -Wall -O2

%.d: %.cpp
	$(CXX) $(DEFS) $(INCLUDES) -E -MD -MF $@ $< >/dev/null

%.o: %.cpp %.d
	$(CXX) $(DEFS) $(INCLUDES) $(CXXFLAGS) -c $< -o $@

%.d: %.c
	$(CC) $(DEFS) -E -MD -MF $@ $< >/dev/null

%.o: %.c %.d
	$(CC) $(DEFS) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	-rm -f *.o *.d *.a $(PROGS)

libnetutil.a: nio.o dns.o http.o debug.o url.o https.o 
	ar rcs libnetutil.a nio.o dns.o http.o debug.o url.o https.o 

nslookup: libnetutil.a  nslookup.o
	$(CXX) $(CXXFLAGS) nslookup.o libnetutil.a -o nslookup
wget: libnetutil.a  wget.o
	$(CXX) $(CXXFLAGS) wget.o -lssl libnetutil.a -o wget
download: libnetutil.a  download.o
	$(CXX) $(CXXFLAGS) download.o libnetutil.a  -o download

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
include https.d
include wget.d
include url.d

