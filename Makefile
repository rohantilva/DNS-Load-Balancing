CC=g++
CXXFLAGS=-std=c++11 -g

miProxy: miProxy.cpp
	$(CC) $(CXXFLAGS) miProxy.cpp -o miProxy

nameserver: nameserver.cpp
	$(CC) $(CXXFLAGS) nameserver.cpp -o nameserver

clean: miProxy nameserver
	rm -fr miProxy nameserver
