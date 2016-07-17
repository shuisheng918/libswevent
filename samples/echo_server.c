#include <sw_event.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libgen.h>

struct sw_ev_context * ctx = NULL;
struct sw_ev_timer *timer = NULL;

void setnonblock(int fd)
{
    fcntl(fd, F_SETFL, O_NONBLOCK | fcntl(fd, F_GETFL));
};

void OnRead(int fd, int events, void * arg)
{
    (void)arg;
    char buf[1024];
    if (events & SW_EV_READ)
    {
        int ret;
        while (1)
        {
            ret = read(fd, buf, sizeof(buf));
            if (ret > 0)
            {
                write(fd, buf, ret);
                continue;
            }
            else if (ret < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                else if (errno != EAGAIN)
                {
                    perror("read");
                    sw_ev_io_del(ctx, fd, SW_EV_READ);
                    close(fd);
                }
            }
            else /* peer close */
            {
                sw_ev_io_del(ctx, fd, SW_EV_READ);
                close(fd);
            }
            break;
        }
    }
}

void OnAcceptReady(int fd, int events, void * arg)
{
    struct sockaddr_in addr;  
    socklen_t addrlen = sizeof(addr);  
    int client;
    
    while (1)
    {
        client = accept(fd, (struct sockaddr *) &addr, &addrlen);
        if (client >= 0)
        {
            setnonblock(client);
            if (-1 == sw_ev_io_add(ctx, client, SW_EV_READ, OnRead, NULL))
            {
                printf("sw_ev_io_add failed, fd=%d\n", client);
            }
        }
        else if (errno == EINTR)
        {
            continue;
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            break;
        }
        else // listen socket occur error
        {
            perror("accept");
            exit(1);
        }
    }
}

void BindAndListen(const char *ip, unsigned short port)
{
    int listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock == -1)
    {
       perror("socket");
       exit(1);
    }
    int reuse = 1;
    if (setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) == -1)
    {
       perror("setsockopt");
       exit(1);
    }
    struct sockaddr_in serverAddr;  
    bzero(&serverAddr,sizeof(struct sockaddr_in));  
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
    setnonblock(listenSock);
    if (-1 == sw_ev_io_add(ctx, listenSock, SW_EV_READ, OnAcceptReady, NULL))
    {
        printf("sw_ev_io_add failed\n");
        close(listenSock);
    }
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        printf("usage: %s <bind_ip> <port>\n", basename(argv[0]));
        exit(1);
    }
    ctx = sw_ev_context_new();
    BindAndListen(argv[1], atoi(argv[2]));
    sw_ev_loop(ctx);
    sw_ev_context_free(ctx);
    return 0;
}
