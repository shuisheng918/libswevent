#include "sw_util.h"
#ifdef _WIN32
#include <windows.h>
#include <sys/timeb.h>
#else
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#endif
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int64_t sw_ev_gettime_ms()
{
#ifdef _WIN32
    struct _timeb tb;
    _ftime(&tb);
    return tb.time * 1000 + tb.millitm;
#else
    struct timeval t;
    gettimeofday(&t, 0);
    return t.tv_sec * 1000 + t.tv_usec / 1000;
#endif
}

int sw_ev_setnonblock(int fd)
{
#ifdef _WIN32
    unsigned long nonblocking = 1;
    return ioctlsocket(fd, FIONBIO, (unsigned long*) &nonblocking);
#else
    int old_flag = fcntl(fd, F_GETFL);
    if (-1 == old_flag)
    {
        return -1;
    }
    return fcntl(fd, F_SETFL, O_NONBLOCK | old_flag);
#endif
}

int sw_ev_socketpair(int fd[2])
{
#ifndef WIN32
    return socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
#else
    int listen_fd = -1;
    int connect_fd = -1;
    int accept_fd = -1;
    struct sockaddr_in listen_addr;
    struct sockaddr_in connect_addr;
    int size;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)    goto oh_no;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listen_addr.sin_port = 0;	/* kernel chooses port.	 */
    if (-1 == bind(listen_fd, (struct sockaddr *) &listen_addr, sizeof (listen_addr)))    goto oh_no;
    if (-1 == listen(listen_fd, 1))    goto oh_no;
    connect_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect_fd < 0) goto oh_no;
    /* We want to find out the port number to connect to.  */
    size = sizeof(connect_addr);
    if (-1 == getsockname(listen_fd, (struct sockaddr *) &connect_addr, &size))    goto oh_no;
    if (-1 == connect(connect_fd, (struct sockaddr *) &connect_addr, sizeof(connect_addr)))    goto oh_no;
    size = sizeof(listen_addr);
    accept_fd = accept(listen_fd, (struct sockaddr *) &listen_addr, &size);
    if (accept_fd < 0) goto oh_no;
    if (getsockname(connect_fd, (struct sockaddr *) &connect_addr, &size) == -1)    goto oh_no;
    //make sure conector is self
    if (listen_addr.sin_family != connect_addr.sin_family
        || listen_addr.sin_addr.s_addr != connect_addr.sin_addr.s_addr
        || listen_addr.sin_port != connect_addr.sin_port)
    {
        goto oh_no;
    }
    SW_EV_CLOSESOCKET(listen_fd);
    fd[0] = connect_fd;
    fd[1] = accept_fd;
    return 0;
 oh_no:
    if (listen_fd != -1)
        SW_EV_CLOSESOCKET(listen_fd);
    if (connect_fd != -1)
        SW_EV_CLOSESOCKET(connect_fd);
    if (accept_fd != -1)
        SW_EV_CLOSESOCKET(accept_fd);
    return -1;
#endif
}