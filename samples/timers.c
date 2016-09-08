#include <stdio.h>
#include "../sw_event.h"
#ifdef _WIN32
#include <windows.h>
#endif

struct sw_ev_context * ctx = NULL;
struct sw_ev_timer *timer[100];

void OnTimer(void *arg)
{
    printf("OnTimer,arg=%d\n", arg);
    sw_ev_timer_del(ctx, timer[(int)arg]);
}

int main(void)
{
    int i;
#ifdef _WIN32
    WSADATA  WsaData;
    if(WSAStartup(MAKEWORD(2,2), &WsaData) != 0 )
    {
          printf("Init Windows Socket Failed: %d\n", GetLastError());
          return -1;
    }
#endif

    ctx = sw_ev_context_new();
    for (i = 0; i < sizeof(timer)/sizeof(struct sw_ev_timer *); ++i)
    {
        timer[i] = sw_ev_timer_add(ctx, 100 * (i + 1), OnTimer, (void*)i);
    }
    sw_ev_loop(ctx);
    sw_ev_context_free(ctx);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
