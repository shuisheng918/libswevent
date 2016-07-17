#include <sw_event.h>
#include <stdio.h>
#include <signal.h>

struct sw_ev_context * ctx = NULL;

void signal_handler(int sig_no, void *arg)
{
    printf("signal %d reache, arg=%p\n", sig_no, arg);
}

int main(void)
{
    ctx = sw_ev_context_new();
    sw_ev_signal_add(ctx, SIGINT, signal_handler, NULL);
    sw_ev_signal_add(ctx, SIGQUIT, signal_handler, NULL);
    sw_ev_loop(ctx);
    sw_ev_context_free(ctx);

    return 0;
}
