#include "../sw_event.h"
#include <stdio.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#endif

struct sw_ev_context * ctx = NULL;

void signal_handler(int sig_no, void *arg)
{
    int count = --*(int*)arg;
    printf("signal %d reache, arg=%d\n", sig_no, count);
    if (0 == count)
    {
        sw_ev_signal_del(ctx, sig_no);
    }
}

int main(void)
{
    int capture_count = 10;
#ifdef _WIN32
    WSADATA  WsaData;
    if(WSAStartup(MAKEWORD(2,2), &WsaData) != 0 )
    {
          printf("Init Windows Socket Failed: %d\n", GetLastError());
          return -1;
    }
#endif
    ctx = sw_ev_context_new();
    sw_ev_signal_add(ctx, SIGINT, signal_handler, &capture_count);
    sw_ev_loop(ctx);
    sw_ev_context_free(ctx);
#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
