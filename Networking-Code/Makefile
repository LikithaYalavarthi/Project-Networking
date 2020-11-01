## Part of the solution for Assignment 3, by Stefan Bruda.

CXX = g++
CXXFLAGS = -w
LDFLAGS = $(CXXFLAGS) -lpthread

.PHONY: bbserv clean distclean

all: bbserv

tcp-utils.o: tcp-utils.h tcp-utils.cc
	$(CXX) $(CXXFLAGS) -c -o tcp-utils.o tcp-utils.cc
tokenize.o: tokenize.h
	$(CXX) $(CXXFLAGS) -c -o tokenize.o tokenize.cc

shserv.o: tcp-utils.h tokenize.h shfd.h shserv.cc
	$(CXX) $(CXXFLAGS) -c -o shserv.o shserv.cc

fserv.o: tcp-utils.h shfd.h fserv.cc
	$(CXX) $(CXXFLAGS) -c -o fserv.o fserv.cc

misc.o: tcp-utils.h shfd.h misc.cc
	$(CXX) $(CXXFLAGS) -c -o misc.o misc.cc

bulletin_threads.o: bulletin_threads.h bulletin_threads.cc
	$(CXX) $(CXXFLAGS) -c -o $@ bulletin_threads.cc

bulletin.o: bulletin.hpp bulletin.cc
	$(CXX) $(CXXFLAGS) -c -o $@ bulletin.cc

sync.o: sync.cc sync.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ sync.cc

bbserv: tokenize.o tcp-utils.o shserv.o fserv.o misc.o bulletin_threads.o bulletin.o sync.o
	$(CXX) $(LDFLAGS) -o $@ tokenize.o tcp-utils.o shserv.o fserv.o misc.o bulletin_threads.o bulletin.o sync.o

## Client:
client.o: tcp-utils.h client.cc
	$(CXX) $(CXXFLAGS) -c -o client.o client.cc

client: client.o tcp-utils.o
	$(CXX) -o client client.o tcp-utils.o

shf: client.o tcp-utils.o
	$(CXX) $(LDFLAGS) -o shf client.o tcp-utils.o

clean:
	rm -f *~ *.o *.bak core \#*

distclean: clean
	rm -f bbserv client *.log *.pid bbfile
