#include "sw_event.h"
#include <sys/types.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <libgen.h>
#include "sw_event_internal.h"
#include "sw_timer_heap.h"
#include "sw_log.h"

void* (*sw_ev_malloc)(size_t) = malloc;
void  (*sw_ev_free)(void *) = free;
void* (*sw_ev_realloc)(void *, size_t) = realloc;

enum { SW_EV_NSIG = 64 };

sw_log_func_t log_func = NULL;

void sw_set_log_function(sw_log_func_t logfunc)
{
    log_func = logfunc;
}

static inline int64_t
sw_ev_gettime_ms_()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec * 1000 + t.tv_usec / 1000;
}
static inline int 
sw_ev_setnonblock_(int fd)
{
    int old_flag = fcntl(fd, F_GETFL);
    if (-1 == old_flag)
    {
        return -1;
    }
    return fcntl(fd, F_SETFL, O_NONBLOCK | old_flag);
}

static volatile struct sw_ev_context * sw_ev_current_signal_context = NULL;

static void
sw_ev_signal_handler_(int sig_no)
{
    unsigned char signal_no = (unsigned char)sig_no;
    if (NULL != sw_ev_current_signal_context)
    {
        write(sw_ev_current_signal_context->signal_pipe[1], &signal_no, 1);
    }
}

static void
sw_ev_sinal_reach_(int fd, int events, void * arg)
{
    (void)events;
    struct sw_ev_context *ctx = (struct sw_ev_context*)arg;
    int ret;
    unsigned char signal_buf[512];
    while (1)
    {
        ret = read(fd, signal_buf, sizeof(signal_buf));
        if (ret > 0)
        {
            int i = 0;
            for (; i < ret; ++i)
            {
                if (signal_buf[i] < SW_EV_NSIG)
                {
                    ctx->signal_events[signal_buf[i]].callback(signal_buf[i], ctx->signal_events[signal_buf[i]].arg);
                }
            }
        }
        else if (ret < 0)
        {
            if (errno == EINTR) continue;
            else if (errno == EAGAIN) break;
            else
            {                      
                sw_log_error_exit("%s:%d read: ", basename(__FILE__), __LINE__, strerror(errno));
            }
        }
        else
        {
            sw_log_error_exit("%s:%d not expected close", basename(__FILE__), __LINE__);
        }
    }
}

struct sw_ev_context * 
sw_ev_context_new()
{
    sw_ev_context_t *ctx = (sw_ev_context_t *)sw_ev_malloc(sizeof(sw_ev_context_t));
    if (NULL == ctx) 
    {
        return NULL;
    }
    ctx->running = 1;
    ctx->current_time = sw_ev_gettime_ms_();
    ctx->io_events_count = 1024;
    ctx->io_events = (sw_ev_io_t *)sw_ev_malloc(sizeof(sw_ev_io_t) * ctx->io_events_count);
    if (NULL == ctx->io_events)
    {
        sw_ev_free(ctx);
        sw_log_error("%s:%d sw_ev_malloc failed", basename(__FILE__), __LINE__);
        return NULL;
    }
    memset(ctx->io_events, 0, sizeof(sw_ev_io_t) * ctx->io_events_count);
    ctx->timer_heap = (sw_timer_heap_t*)sw_ev_malloc(sizeof(sw_timer_heap_t));
    if (NULL == ctx->timer_heap)
    {
        sw_ev_free(ctx->io_events);
        sw_ev_free(ctx); 
        sw_log_error("%s:%d sw_ev_malloc failed", basename(__FILE__), __LINE__);
        return NULL;
    }
    sw_timer_heap_ctor(ctx->timer_heap);
    ctx->prepare.callback = NULL;
    ctx->prepare.arg = NULL;
    ctx->check.callback = NULL;
    ctx->check.arg = NULL;
    ctx->epoll_fd = epoll_create(4096);
    if (-1 == ctx->epoll_fd)
    {
        sw_log_error_exit("%s:%d epoll_create: %s", basename(__FILE__), __LINE__, strerror(errno));
    }
    ctx->signal_events = (sw_ev_signal_t *)sw_ev_malloc(SW_EV_NSIG * sizeof(sw_ev_signal_t));
    if (NULL == ctx->signal_events)
    {
        close(ctx->epoll_fd);
        sw_ev_free(ctx->io_events);
        sw_timer_heap_dtor(ctx->timer_heap);
        sw_ev_free(ctx->timer_heap);
        sw_ev_free(ctx);
        sw_log_error("%s:%d sw_ev_malloc failed", basename(__FILE__), __LINE__);
        return NULL;
    }
    memset(ctx->signal_events, 0, SW_EV_NSIG * sizeof(sw_ev_signal_t));
    if (-1 == socketpair(AF_LOCAL, SOCK_STREAM, 0, ctx->signal_pipe))
    {
        sw_log_error_exit("%s:%d socketpair: %s", basename(__FILE__), __LINE__, strerror(errno));
    }
    if (-1 == sw_ev_setnonblock_(ctx->signal_pipe[0]))
    {
        sw_log_error_exit("%s:%d sw_ev_setnonblock: %s", basename(__FILE__), __LINE__, strerror(errno));
    }
    if (-1 == sw_ev_setnonblock_(ctx->signal_pipe[1]))
    {
        sw_log_error_exit("%s:%d sw_ev_setnonblock: %s", basename(__FILE__), __LINE__, strerror(errno));
    }
    sw_ev_io_add(ctx, ctx->signal_pipe[0], SW_EV_READ, sw_ev_sinal_reach_, ctx);
    return ctx;
}

void 
sw_ev_context_free(struct sw_ev_context *ctx)
{
    if (NULL != ctx)
    {
        if (ctx == sw_ev_current_signal_context)
        {
            int i = 0;
            for (; i < SW_EV_NSIG; ++i)
            {
                sw_ev_signal_del(ctx, i);
            }
            __sync_bool_compare_and_swap(&sw_ev_current_signal_context, ctx, NULL);
        }
        close(ctx->signal_pipe[0]);
        close(ctx->signal_pipe[1]);
        sw_ev_free(ctx->signal_events);
        close(ctx->epoll_fd);
        unsigned i = 0;
        for (; i < ctx->timer_heap->size; ++i)
        {
            sw_ev_free(ctx->timer_heap->timers[i]);
        }
        sw_timer_heap_dtor(ctx->timer_heap);
        sw_ev_free(ctx->timer_heap);
        sw_ev_free(ctx->io_events);
        sw_ev_free(ctx);
    }
}

static int 
expand_io_events_(sw_ev_context_t *ctx, int max_fd)
{
    int capacity = ctx->io_events_count;
    while (capacity <= max_fd)
    {
        capacity <<= 1;
    }
    sw_ev_io_t *events;
    events = (sw_ev_io_t *)sw_ev_realloc(ctx->io_events, capacity * sizeof(sw_ev_io_t));
    if (NULL == events)
    {
        sw_log_error("%s:%d sw_ev_realloc: %s", basename(__FILE__), __LINE__, strerror(errno));
        return -1;
    }
    ctx->io_events_count = capacity;
    ctx->io_events = events;
    return 0;
}

int
sw_ev_io_add(struct sw_ev_context *ctx, int fd, int what_events,
             void (*callback)(int fd, int events, void *arg),
             void *arg)
{
    if (fd < 0) return -1;
    if (fd >= ctx->io_events_count)
    {
        if (-1 == expand_io_events_(ctx, fd))
        {
            return -1;
        }
    }
    what_events &= SW_EV_READ | SW_EV_WRITE;
    if (!what_events)
    {
        return -1;
    }
	struct epoll_event ev;
	ev.events = EPOLLET | EPOLLPRI | EPOLLERR | EPOLLHUP;
    ev.data.u64 = fd;
    int op = EPOLL_CTL_ADD;
    sw_ev_io_t *ioevent = &ctx->io_events[fd];
    int now_care_what_events = ioevent->events | what_events;
    if (now_care_what_events & SW_EV_READ)
    {
        ev.events |= EPOLLIN;
    }
    if (now_care_what_events & SW_EV_WRITE)
    {
        ev.events |= EPOLLOUT;
    }
    if (ioevent->events)
    {
        op = EPOLL_CTL_MOD;
    }
    if (epoll_ctl(ctx->epoll_fd, op, fd, &ev) != 0)
	{
		sw_log_error("%s:%d epoll_ctl: %s", basename(__FILE__), __LINE__, strerror(errno));
        return -1;
	}
    ioevent->events = now_care_what_events;
    ioevent->callback = callback;
    ioevent->arg = arg;
    return 0;
}

int
sw_ev_io_del(struct sw_ev_context *ctx, int fd, int what_events)
{
    if (fd < 0 || fd >= ctx->io_events_count)
    {
        return -1;
    }
    what_events &= SW_EV_READ | SW_EV_WRITE;
    if (!what_events)
    {
        return -1;
    }
    struct epoll_event ev;
    ev.events = EPOLLET | EPOLLPRI | EPOLLERR | EPOLLHUP;
    ev.data.u64 = fd;
    int op = EPOLL_CTL_DEL;
    sw_ev_io_t *ioevent = &ctx->io_events[fd];
    if (!ioevent->events)
    {
        return 0;
    }
    int now_care_what_events = ~what_events & ioevent->events;
    if (now_care_what_events & SW_EV_READ)
    {
        ev.events |= EPOLLIN;
    }
    if (now_care_what_events & SW_EV_WRITE)
    {
        ev.events |= EPOLLOUT;
    }
    if (now_care_what_events)
    {
        op = EPOLL_CTL_MOD;
    }
	if (epoll_ctl(ctx->epoll_fd, op, fd, &ev) != 0)
	{
		sw_log_error("%s:%d epoll_ctl: %s", basename(__FILE__), __LINE__, strerror(errno));
        return -1;
	}
    ioevent->events = now_care_what_events;
    if (!ioevent->events)
    {
        ioevent->callback = NULL;
        ioevent->arg = NULL;
    }
    return 0;
}

struct sw_ev_timer * 
sw_ev_timer_add(struct sw_ev_context *ctx, int timeout_ms,
                void (*callback)(void *arg),
                void *arg)
{
    if (timeout_ms <= 0)
    {
        return NULL;
    }
    struct sw_ev_timer *timer = sw_ev_malloc(sizeof(struct sw_ev_timer));
    if (NULL == timer)
    {
        return NULL;
    }
    sw_timer_heap_elem_init(timer);
    timer->callback = callback;
    timer->arg = arg;
    timer->interval = timeout_ms;
    timer->next_expire_time = ctx->current_time + timeout_ms;
    if (-1 == sw_timer_heap_push(ctx->timer_heap, timer))
    {
        sw_ev_free(timer);
        return NULL;
    }
    return timer;
}

int
sw_ev_timer_del(struct sw_ev_context *ctx, struct sw_ev_timer *timer)
{
    if (NULL == timer)
    {
        return -1;
    }
    if (timer->index_in_heap != (unsigned)-1)
    {
        sw_timer_heap_erase(ctx->timer_heap, timer);
    }
    sw_ev_free(timer);
    return 0;
}

int
sw_ev_signal_add(struct sw_ev_context *ctx, int sig_no,
                 void (*callback)(int sig_no, void *arg),
                 void *arg)
{
    if (sig_no < 0 || sig_no >= SW_EV_NSIG)
    {
        return -1;
    }
    if (!__sync_bool_compare_and_swap(&sw_ev_current_signal_context, NULL, ctx))
    {
        if (sw_ev_current_signal_context != ctx)
        {
            sw_log_error("%s:%d sw_ev_signal_add: Please register signal handler in "
                         "the same event context.", 
                         basename(__FILE__), __LINE__);
            return -1;
        }
    }
    signal(sig_no, sw_ev_signal_handler_);
    ctx->signal_events[sig_no].callback = callback;
    ctx->signal_events[sig_no].arg = arg;
    return 0;
}

int
sw_ev_signal_del(struct sw_ev_context *ctx, int sig_no)
{
    if (sig_no < 0 || sig_no >= SW_EV_NSIG)
    {
        return -1;
    }
    if (NULL == sw_ev_current_signal_context)
    {
        return -1;
    }
    if (ctx != sw_ev_current_signal_context)
    {
        sw_log_error("%s:%d sw_ev_signal_del: Please register and unregister sinal handler in "
                     "the same event context.", 
                     basename(__FILE__), __LINE__);
        return -1;
    }
    signal(sig_no, SIG_DFL);
    ctx->signal_events[sig_no].callback = NULL;
    ctx->signal_events[sig_no].arg = NULL;
    return 0;
}

void sw_ev_prepare_set(struct sw_ev_context *ctx,
                  void (*callback)(void* arg),
                  void *arg)
{
    ctx->prepare.callback = callback;
    ctx->prepare.arg = arg;
}

void sw_ev_check_set(struct sw_ev_context *ctx,
                void (*callback)(void* arg),
                void *arg)
{
    ctx->check.callback = callback;
    ctx->check.arg = arg;
}


/*
 * process the expired timers, and return next poll wait time(ms).
 */
static int
process_timers_(struct sw_ev_context *ctx)
{
    int64_t curtime = ctx->current_time;
    sw_timer_heap_t * heap = ctx->timer_heap;
    struct sw_ev_timer * top_timer = sw_timer_heap_top(heap);
    while (NULL != top_timer && curtime >= top_timer->next_expire_time)
    {
        if (NULL != top_timer->callback)
        {
            sw_timer_heap_pop(heap);
            top_timer->next_expire_time += top_timer->interval;
            sw_timer_heap_push(heap, top_timer);
            top_timer->callback(top_timer->arg);
        }
        top_timer = sw_timer_heap_top(heap);
    }
    
    int next_wait_time = 0;
    if (NULL != top_timer)
    {
        next_wait_time = top_timer->next_expire_time - curtime;
    }
    /* Next poll wait time is 1800000ms(30 minutes) at most. */
    if (0 == next_wait_time || next_wait_time > 1800000)
    {
        next_wait_time = 1800000;
    }
    return next_wait_time;
}

int
sw_ev_loop(struct sw_ev_context *ctx)
{
    struct epoll_event ready_events[4096];
    int nfds = 0;
    int i = 0;
    int wait_time = -1;
    while (ctx->running)
    {
        ctx->current_time = sw_ev_gettime_ms_();
        wait_time = process_timers_(ctx);
        if (NULL != ctx->prepare.callback)
        {
            ctx->prepare.callback(ctx->prepare.arg);
        }
		nfds = epoll_wait(ctx->epoll_fd, ready_events, sizeof(ready_events)/sizeof(struct epoll_event),  wait_time);
        if (nfds == -1)
        {
    		if (errno != EINTR)
            {
                sw_log_error("%s:%d epoll_wait: %s", basename(__FILE__), __LINE__, strerror(errno));
    			return -1;
    		}
		}
		for (i = 0; i < nfds; i++)
		{
			int ev_fd = ready_events[i].data.fd;
			sw_ev_io_t *ioevent = &ctx->io_events[ev_fd];
            int what_events = 0;
			if (ready_events[i].events & EPOLLIN)
			{
				what_events |= SW_EV_READ;
			}
			if (ready_events[i].events & EPOLLOUT)
			{
				what_events |= SW_EV_WRITE;
			}
			if (ready_events[i].events & (EPOLLPRI | EPOLLERR | EPOLLHUP))
			{
				what_events |= SW_EV_READ;
			}
            if (what_events && NULL != ioevent->callback)
            {
                ioevent->callback(ev_fd, what_events, ioevent->arg);
            }
		}
        if (NULL != ctx->check.callback)
        {
            ctx->check.callback(ctx->check.arg);
        }
	}
    return 0;
}

void
sw_ev_loop_exit(struct sw_ev_context *ctx)
{
    ctx->running = 0;
}

void
sw_ev_set_memory_func(void* (*malloc_func)(size_t),
                      void  (*free_func)(void *),
                      void* (*realloc_func)(void *, size_t))
{
    sw_ev_malloc = malloc_func;
    sw_ev_free = free_func;
    sw_ev_realloc = realloc_func;
}


