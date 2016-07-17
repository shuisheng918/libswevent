# libsmallwater_event

----------

- libsmallwater_event is a light weight net event library. 
- Support events : socket read write, timer, signal, prepare, check.
- Similar to libevent, redesign a event library just because we want more simple to use, more efficient and less memory.
- Currently supporting platform: linux.

# build
    make
    make PREFIX=/usr/local install

# usage
    g++ yourcode.cpp -lsmallwater_event