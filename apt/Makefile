default: nslookup
INCLUDES = -include ./debug.h
CXXFLAGS = -g -fsanitize=address -Wall -O2

%.d: %.cpp
	$(CXX) $(INCLUDES) -E -MD -MF $@ $< >/dev/null

%.o: %.cpp %.d
	$(CXX) $(INCLUDES) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	-rm -f *.o *.d *.a

nslookup: nio.o dns.o debug.o nslookup.o
	$(CXX) $(CXXFLAGS) nio.o debug.o dns.o nslookup.o -o nslookup
wget: nio.o http.o debug.o wget.o
	$(CXX) $(CXXFLAGS) nio.o debug.o http.o wget.o -o wget

include debug.d
include dns.d
include nslookup.d
include nio.d
include http.d
include wget.d
