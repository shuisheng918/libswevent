#include "../sw_event.h"
#include <sys/types.h>
#ifdef _WIN32
#include <windows.h>
#else 
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#else
#include <sys/epoll.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct sw_ev_context * ctx = NULL;
struct sw_ev_timer *timer = NULL;

void OnIOReady(int fd, int events, void * arg);

struct SendBuf
{
    char *buf;
    int len;
    int offset;
    struct SendBuf * next;
};

struct Session
{
    int fd;
    struct SendBuf * sendHead;
    struct SendBuf * sendTail;
};

void AppendBuf(struct Session *pSession, const char *buf, int len)
{
    struct SendBuf * node;
    if (NULL == pSession || NULL == buf || len <= 0)  return;

    node = (struct SendBuf * )malloc(sizeof(struct SendBuf));
    assert(node);
    node->len = len;
    node->offset = 0;
    node->next = NULL;
    node->buf = (char*)malloc(len);
    assert(node->buf);
    memcpy((void*)node->buf, buf, len);

    if (pSession->sendTail)
    {
        pSession->sendTail->next = node;
        pSession->sendTail = node;
    }
    else
    {
        pSession->sendHead = node;
        pSession->sendTail = node;
    }
}

void EndSession(struct Session *pSession)
{
    struct SendBuf *pTmp, *pNext;
    sw_ev_io_del(ctx, pSession->fd, SW_EV_READ | SW_EV_WRITE);
    SW_EV_CLOSESOCKET(pSession->fd);
    pSession->fd = -1;
    for (pNext = pSession->sendHead; pNext != NULL; )
    {
        if (pNext->buf)
        {
            free((void*)pNext->buf);
        }
        pTmp = pNext->next;
        free(pNext);
        pNext = pTmp;
    }
    pSession->sendHead = NULL;
    pSession->sendTail = NULL;
    free(pSession);
}

void SendData(struct Session *pSession)
{
    int ret;
    struct SendBuf *pNext, *pTmp;
    if (NULL == pSession) return;

    for (pNext = pSession->sendHead; pNext != NULL; pNext = pNext->next)
    {
resend:
        ret = send(pSession->fd, pNext->buf + pNext->offset, pNext->len - pNext->offset, 0);
        if (ret == pNext->len - pNext->offset)
        {
            continue;
        }
        else if(ret >= 0)
        {
            pNext->offset += ret;
            if (-1 == sw_ev_io_add(ctx, pSession->fd, SW_EV_WRITE, OnIOReady, pSession))
            {
                EndSession(pSession);
                return;
            }
            break;
        }
        else
        {
#ifdef _WIN32
            if (SW_ERRNO == WSAEINTR) goto resend;
            else if (SW_ERRNO == WSAEWOULDBLOCK)
#else
            if (SW_ERRNO == EINTR) goto resend;
            else if (SW_ERRNO == EWOULDBLOCK)
#endif
            {
                if (-1 == sw_ev_io_add(ctx, pSession->fd, SW_EV_WRITE, OnIOReady, pSession))
                {
                    EndSession(pSession);
                    return;
                }
                break;
            }
            else
            {
                EndSession(pSession);
                return;
            }
        }
    }
    if (pNext == NULL)
    {
        sw_ev_io_del(ctx, pSession->fd, SW_EV_WRITE);
    }
    for (; pSession->sendHead != pNext && pSession->sendHead != NULL; )
    {
        free(pSession->sendHead->buf);
        pTmp = pSession->sendHead->next;
        free(pSession->sendHead);
        pSession->sendHead = pTmp;
    }
    if (NULL == pSession->sendHead)
    {
        pSession->sendTail = NULL;
    }
}


void OnIOReady(int fd, int events, void * arg)
{
    struct Session *pSession = (struct Session *)arg;
    char buf[1024];
    assert(pSession);
    assert(fd == pSession->fd);
    if (events & SW_EV_READ)
    {
        int ret;
        while (1)
        {
            ret = recv(fd, buf, sizeof(buf), 0);
            if (ret > 0)
            {
                AppendBuf(pSession, buf, ret);
                continue;
            }
            else if (ret < 0)
            {
#ifdef _WIN32
                if(SW_ERRNO == WSAEINTR)    continue;
                else if (SW_ERRNO != WSAEWOULDBLOCK)
#else
                if (SW_ERRNO == EINTR)    continue;
                else if (SW_ERRNO != EAGAIN)
#endif
                {
                    EndSession(pSession);
                    return;
                }
            }
            else /* peer close */
            {
                EndSession(pSession);
                return;
            }
            break;
        }
        if (pSession)
        {
            SendData(pSession);
        }
    }
    if (events & SW_EV_WRITE)
    {
        SendData(pSession);
    }
}

void OnAcceptReady(int fd, int events, void * arg)
{
    struct sockaddr_in addr;  
#ifdef _WIN32
    int addrlen;
#else
    socklen_t addrlen;
#endif
    int client;
    struct Session *pSession;

    while (1)
    {
        addrlen = sizeof(addr);  
        client = accept(fd, (struct sockaddr *) &addr, &addrlen);
        printf("fd=%d, client=%d\n", fd, client);
        if (client >= 0)
        {
            sw_ev_setnonblock(client);
            pSession = (struct Session *)malloc(sizeof(struct Session));
            assert(pSession);
            pSession->fd = client;
            pSession->sendHead = NULL;
            pSession->sendTail = NULL;
            if (-1 == sw_ev_io_add(ctx, client, SW_EV_READ, OnIOReady, pSession))
            {
                free(pSession);
                printf("sw_ev_io_add failed, fd=%d\n", client);
            }
        }
#ifdef _WIN32
        else if (SW_ERRNO == WSAEINTR)    continue;
        else if (SW_ERRNO == WSAEWOULDBLOCK)    break;
#else
        else if (SW_ERRNO == EINTR)    continue;
        else if (SW_ERRNO == EAGAIN)    break;
#endif
        else // listen socket occur error
        {
            printf("accept: %d\n", SW_ERRNO);
            exit(1);
        }
    }
}

void BindAndListen(const char *ip, unsigned short port)
{
    int listenSock = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    struct sockaddr_in serverAddr;  
    if (listenSock == -1)
    {
        perror("socket");
        exit(1);
    }

    if (setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) == -1)
    {
        perror("setsockopt");
        exit(1);
    }

    memset(&serverAddr, 0, sizeof(struct sockaddr_in));  
    serverAddr.sin_family = AF_INET;  
    serverAddr.sin_addr.s_addr = inet_addr(ip);
    serverAddr.sin_port = htons(port);

    if(bind(listenSock,(struct sockaddr *)(&serverAddr),sizeof(struct sockaddr)) == -1)  
    {  
        perror("bind");
        exit(1);
    }  
    if (listen(listenSock, 128) == -1)
    {  
        perror("listen");
        exit(1);
    }
    sw_ev_setnonblock(listenSock);
    if (-1 == sw_ev_io_add(ctx, listenSock, SW_EV_READ, OnAcceptReady, NULL))
    {
        printf("sw_ev_io_add failed\n");
        SW_EV_CLOSESOCKET(listenSock);
    }
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    WSADATA  WsaData;
    if(WSAStartup(MAKEWORD(2,2), &WsaData) != 0 )
    {
        printf("Init Windows Socket Failed: %d\n", GetLastError());
        return -1;
    }
#endif
    if (argc != 3)
    {
        printf("usage: %s <bind_ip> <port>\n", argv[0]);
        exit(1);
    }
    ctx = sw_ev_context_new();
    BindAndListen(argv[1], atoi(argv[2]));
    sw_ev_loop(ctx);
    sw_ev_context_free(ctx);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
