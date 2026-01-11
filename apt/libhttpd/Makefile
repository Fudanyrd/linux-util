all: libhttpd.a
SRCS := $(wildcard *.cpp)
OBJS := $(SRCS:.cpp=.o)

CXX = g++
CXXFLAGS = -O2 -g

%.o: %.cpp
	@$(CXX) $(CXXFLAGS) $< -MD -c -o $@

libhttpd.a: $(OBJS)
	@$(AR) rcs $@ $^

test: httpd-test.o libhttpd.a 
	@$(CXX) $(CXXFLAGS) -o $@ $^ -lssl

-include *.d

