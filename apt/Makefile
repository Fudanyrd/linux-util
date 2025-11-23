PROGS = nslookup wget download
PROGS += apt
PROGS += apt-debug

default: $(PROGS)
all: $(PROGS)
DEFS = -DCONFIG_HAS_SSL
INCLUDES = -include ./debug.h
CXXFLAGS = -g -fsanitize=address -Wall -O2
CXXFLAGS += -Wno-pointer-arith
CXXFLAGS += -std=c++17

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

libnetutil.a: nio.o dns.o http.o debug.o url.o https.o 
	ar rcs libnetutil.a nio.o dns.o http.o debug.o url.o https.o 

nslookup: libnetutil.a  nslookup.o
	$(CXX) $(CXXFLAGS) nslookup.o libnetutil.a -o nslookup
wget: libnetutil.a  wget.o
	$(CXX) $(CXXFLAGS) wget.o -lssl libnetutil.a -o wget
download: libnetutil.a  download.o
	$(CXX) $(CXXFLAGS) download.o libnetutil.a  -o download

apt: apt.o aptlist.o libnetutil.a
	$(CXX) $(CXXFLAGS) apt.o aptlist.o libnetutil.a -o apt

apt-debug: apt
	@ln -sf $(shell realpath apt) apt-debug

-include *.d

