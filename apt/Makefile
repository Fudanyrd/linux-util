PROGS = nslookup wget download

default: $(PROGS)
INCLUDES = -include ./debug.h
CXXFLAGS = -g -fsanitize=address -Wall -O2

%.d: %.cpp
	$(CXX) $(INCLUDES) -E -MD -MF $@ $< >/dev/null

%.o: %.cpp %.d
	$(CXX) $(INCLUDES) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	-rm -f *.o *.d *.a $(PROGS)

nslookup: nio.o dns.o debug.o nslookup.o
	$(CXX) $(CXXFLAGS) nio.o debug.o dns.o nslookup.o -o nslookup
wget: nio.o http.o debug.o wget.o
	$(CXX) $(CXXFLAGS) nio.o debug.o http.o wget.o -lssl -o wget
download: nio.o http.o debug.o download.o dns.o
	$(CXX) $(CXXFLAGS) nio.o debug.o http.o dns.o download.o -lssl -o download

include debug.d
include download.d
include dns.d
include nslookup.d
include nio.d
include http.d
include wget.d
