# libswevent

----------

- libswevent is a light weight net event library. 
- Support events : socket read write, timer, signal, prepare, check.
- Similar to libevent, redesign a event library just because we want more simple to use, more efficient and less memory.
- Currently supporting platform: linux(use epoll), Windows(use select), FreeBSD(use kqueue), MAC(use kqueue, have not test).

# build
    make
    make install

Default install path is /usr/local, you can modify variable 'INSTALL_DIR' in makefile.

# usage
    g++ yourcode.cpp -lswevent

# Windows
    swevent.a and samples can be build in visual stuido 2012 projects. See windows_project\libswevent 

