#include <sw_event.h>
#include <stdio.h>


struct sw_ev_context * ctx = NULL;
struct sw_ev_timer *timer[100];

void OnTimer(void *arg)
{
    printf("OnTimer,arg=%d\n", arg);
    sw_ev_timer_del(ctx, timer[(int)arg]);
}

int main(void)
{
    ctx = sw_ev_context_new();
    for (int i = 0; i < sizeof(timer)/sizeof(struct sw_ev_timer *); ++i)
    {
        timer[i] = sw_ev_timer_add(ctx, 100 * (i + 1), OnTimer, (void*)i);
    }
    sw_ev_loop(ctx);
    sw_ev_context_free(ctx);

    return 0;
}
