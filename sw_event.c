#include "sw_event.h"
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
    #include <unistd.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include "sw_timer_heap.h"
#include "sw_log.h"
#include "sw_util.h"

void* (*sw_ev_malloc)(size_t) = malloc;
void  (*sw_ev_free)(void *) = free;
void* (*sw_ev_realloc)(void *, size_t) = realloc;

#ifndef NSIG  /* at most support NSIG signals */
#define NSIG (64)
#endif

enum { SW_EV_NSIG = NSIG };

sw_log_func_t log_func = NULL;

void sw_set_log_func(sw_log_func_t logfunc)
{
    log_func = logfunc;
}

static volatile sw_ev_context_t * sw_ev_current_signal_context = NULL;

static void
sw_ev_signal_handler_(int sig_no)
{
    unsigned char signal_no = (unsigned char)sig_no;
#ifdef _WIN32
    signal(sig_no, sw_ev_signal_handler_);
#endif
    if (NULL != sw_ev_current_signal_context)
    {
        send(sw_ev_current_signal_context->signal_pipe[1], &signal_no, 1, 0);
    }
}

static void
sw_ev_sinal_reach_(int fd, int events, void * arg)
{
    int ret;
    sw_ev_context_t *ctx = (sw_ev_context_t *)arg;
    
    unsigned char signal_buf[512];
    while (1)
    {
        ret = recv(fd, signal_buf, sizeof(signal_buf), 0);
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
#ifdef _WIN32
            if (SW_ERRNO == WSAEINTR)  continue;
            else if (SW_ERRNO == WSAEWOULDBLOCK) break;
            else
            {                      
                sw_log_error_exit("%s:%d recv: %d", __FILE__, __LINE__, SW_ERRNO);
            }
#else
            if (SW_ERRNO == EINTR) continue;
            else if (SW_ERRNO == EAGAIN) break;
            else
            {
                sw_log_error_exit("%s:%d recv: %d", __FILE__, __LINE__, SW_ERRNO);
            }
#endif
        }
        else
        {
            sw_log_error_exit("%s:%d not expected close", __FILE__, __LINE__);
        }
    }
}

sw_ev_context_t * 
sw_ev_context_new()
{
    sw_ev_context_t *ctx = (sw_ev_context_t *)sw_ev_malloc(sizeof(sw_ev_context_t));
    if (NULL == ctx) 
    {
        return NULL;
    }
    ctx->running = 1;
    ctx->current_time = sw_ev_gettime_ms();
    ctx->io_events_count = 1024;
    ctx->io_events = (sw_ev_io_t *)sw_ev_malloc(sizeof(sw_ev_io_t) * ctx->io_events_count);
    if (NULL == ctx->io_events)
    {
        sw_log_error("%s:%d sw_ev_malloc failed", __FILE__, __LINE__);
        goto oh_no;
    }
    memset(ctx->io_events, 0, sizeof(sw_ev_io_t) * ctx->io_events_count);
    ctx->timer_heap = (sw_timer_heap_t*)sw_ev_malloc(sizeof(sw_timer_heap_t));
    if (NULL == ctx->timer_heap)
    {
        sw_log_error("%s:%d sw_ev_malloc failed", __FILE__, __LINE__);
        goto oh_no;
    }
    sw_timer_heap_ctor(ctx->timer_heap);
    memset(ctx->prepares, 0, (sizeof(sw_ev_prepare_t *) * SW_EV_MAX_PREPARE));
    ctx->prepares_count = 0;
    memset(ctx->checks, 0, (sizeof(sw_ev_check_t *) * SW_EV_MAX_CHECK));
    ctx->checks_count = 0;
#ifdef _WIN32
    FD_ZERO(&ctx->read_set);
    FD_ZERO(&ctx->write_set);
    FD_ZERO(&ctx->except_set);
#elif defined(__APPLE__) || defined(__FreeBSD__)
    ctx->kqueue_fd = kqueue();
    if (-1 == ctx->kqueue_fd)
    {
        sw_log_error_exit("%s:%d kqueue: %d", __FILE__, __LINE__, SW_ERRNO);
    }
#else /* linux */
    ctx->epoll_fd = epoll_create(4096);
    if (-1 == ctx->epoll_fd)
    {
        sw_log_error_exit("%s:%d epoll_create: %d", __FILE__, __LINE__, SW_ERRNO);
    }
#endif
    ctx->signal_events = (sw_ev_signal_t *)sw_ev_malloc(SW_EV_NSIG * sizeof(sw_ev_signal_t));
    if (NULL == ctx->signal_events)
    {
        sw_log_error("%s:%d sw_ev_malloc failed", __FILE__, __LINE__);
        goto oh_no;
    }
    memset(ctx->signal_events, 0, SW_EV_NSIG * sizeof(sw_ev_signal_t));
    if (-1 == sw_ev_socketpair(ctx->signal_pipe))
    {
        sw_log_error_exit("%s:%d sw_ev_socketpair: %d", __FILE__, __LINE__, SW_ERRNO);
    }
    if (-1 == sw_ev_setnonblock(ctx->signal_pipe[0]))
    {
        sw_log_error_exit("%s:%d sw_ev_setnonblock: %d", __FILE__, __LINE__, SW_ERRNO);
    }
    if (-1 == sw_ev_setnonblock(ctx->signal_pipe[1]))
    {
        sw_log_error_exit("%s:%d sw_ev_setnonblock: %d", __FILE__, __LINE__, SW_ERRNO);
    }
    if (-1 == sw_ev_io_add(ctx, ctx->signal_pipe[0], SW_EV_READ, sw_ev_sinal_reach_, ctx))
    {
        sw_log_error_exit("%s:%d sw_ev_io_add: %d", __FILE__, __LINE__, SW_ERRNO);
    }
    return ctx;
oh_no:
    if (NULL != ctx)
    {
#ifdef _WIN32
        //nothing
#elif defined(__APPLE__) || defined(__FreeBSD__)
        if (ctx->kqueue_fd != -1)    close(ctx->kqueue_fd);
#else
        if (ctx->epoll_fd != -1)    close(ctx->epoll_fd);
#endif
        if (NULL != ctx->io_events)
        {
            sw_ev_free(ctx->io_events);
        }
        if (NULL != ctx->timer_heap)
        {
            sw_timer_heap_dtor(ctx->timer_heap);
            sw_ev_free(ctx->timer_heap);
        }
        sw_ev_free(ctx);
    }
    return NULL;
}

void 
sw_ev_context_free(sw_ev_context_t *ctx)
{
    if (NULL != ctx)
    {
        unsigned i;
        if (ctx == sw_ev_current_signal_context)
        {
            for (i = 0; i < SW_EV_NSIG; ++i)
            {
                sw_ev_signal_del(ctx, i);
            }
            CAS(&sw_ev_current_signal_context, ctx, NULL);
        }
        SW_EV_CLOSESOCKET(ctx->signal_pipe[0]);
        SW_EV_CLOSESOCKET(ctx->signal_pipe[1]);
        sw_ev_free(ctx->signal_events);
#ifdef _WIN32
        //nothing
#elif defined(__APPLE__) || defined(__FreeBSD__)
        if (ctx->kqueue_fd != -1)    close(ctx->kqueue_fd);
#else
        if (ctx->epoll_fd != -1)    close(ctx->epoll_fd);
#endif
        for (i = 0; i < ctx->prepares_count; ++i)
        {
            if (ctx->prepares[i])
            {
                sw_ev_free(ctx->prepares[i]);
            }
        }
        for (i = 0; i < ctx->checks_count; ++i)
        {
            if (ctx->checks[i])
            {
                sw_ev_free(ctx->checks[i]);
            }
        }
        for (i = 0; i < ctx->timer_heap->size; ++i)
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
    sw_ev_io_t *events;
    while (capacity <= max_fd)
    {
        capacity <<= 1;
    }
    events = (sw_ev_io_t *)sw_ev_realloc(ctx->io_events, capacity * sizeof(sw_ev_io_t));
    if (NULL == events)
    {
        sw_log_error("%s:%d sw_ev_realloc: %d", __FILE__, __LINE__, SW_ERRNO);
        return -1;
    }
    ctx->io_events_count = capacity;
    ctx->io_events = events;
    return 0;
}

/*
 * process the expired timers, and return next poll wait time(ms).
 */
static int
process_timers_(sw_ev_context_t *ctx)
{
    int64_t curtime = ctx->current_time;
    sw_timer_heap_t * heap = ctx->timer_heap;
    int next_wait_time = 0;
    sw_ev_timer_t * top_timer = sw_timer_heap_top(heap);
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
    if (NULL != top_timer)
    {
        next_wait_time = (int)(top_timer->next_expire_time - curtime);
    }
    /* Next poll wait time is 1800000ms(30 minutes) at most. */
    if (0 == next_wait_time || next_wait_time > 1800000)
    {
        next_wait_time = 1800000;
    }
    return next_wait_time;
}

#ifdef _WIN32
int
sw_ev_io_add(sw_ev_context_t *ctx, int fd, int what_events,
             void (*callback)(int fd, int events, void *arg),
             void *arg)
{
    sw_ev_io_t *ioevent;
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
    if (what_events & SW_EV_READ)
    {
        if (ctx->read_set.fd_count >= FD_SETSIZE)
        {
            return -1;
        }
        FD_SET(fd, &ctx->read_set);
    }
    if (what_events & SW_EV_WRITE)
    {
        if (ctx->write_set.fd_count >= FD_SETSIZE)
        {
            return -1;
        }
        FD_SET(fd, &ctx->write_set);
    }
    ioevent = &ctx->io_events[fd];
    ioevent->events |= what_events;
    ioevent->callback = callback;
    ioevent->arg = arg;
    return 0;
}

int
sw_ev_io_del(sw_ev_context_t *ctx, int fd, int what_events)
{
    sw_ev_io_t *ioevent;
    if (fd < 0 || fd >= ctx->io_events_count)
    {
        return -1;
    }
    what_events &= SW_EV_READ | SW_EV_WRITE;
    if (!what_events)
    {
        return -1;
    }
    if (what_events & SW_EV_READ)
    {
        FD_CLR(fd, &ctx->read_set);
    }
    if (what_events & SW_EV_WRITE)
    {
        FD_CLR(fd, &ctx->write_set);
    }
    ioevent = &ctx->io_events[fd];
    ioevent->events &= ~what_events;
    if (!ioevent->events)
    {
        ioevent->callback = NULL;
        ioevent->arg = NULL;
    }
    return 0;
}

struct sw_ev_fd_list
{
    int  fds[3*FD_SETSIZE];
    int  events[3*FD_SETSIZE];
    int  fd_count;
};

static void push_to_result_fd_list(struct sw_ev_fd_list * fd_list, int fd, int what_events)
{
    int i = 0;
    for ( ; i < fd_list->fd_count; ++i)
    {
        if (fd_list->fds[i] == fd)
        {
            fd_list->events[i] |= what_events;
            return;
        }
    }
    if (fd_list->fd_count < 3*FD_SETSIZE)
    {
        fd_list->fds[fd_list->fd_count] = fd;
        fd_list->events[fd_list->fd_count] = what_events;
        ++fd_list->fd_count;
    }
}

int
sw_ev_loop(sw_ev_context_t *ctx)
{
    fd_set read_set, write_set, except_set;
    int nfds = 0;
    int i = 0;
    int wait_time = -1;
    struct timeval tv = {0, 0};
    struct sw_ev_fd_list res_fd_list;
    int fd;
    sw_ev_io_t *ioevent = NULL;
    while (ctx->running)
    {
        ctx->current_time = sw_ev_gettime_ms();
        wait_time = process_timers_(ctx);
        for (i = 0; i < ctx->prepares_count; ++i)
        {
            if (NULL != ctx->prepares[i]->callback)
            {
                ctx->prepares[i]->callback(ctx->prepares[i]->arg);
            }
        }
        tv.tv_sec = wait_time / 1000;
        tv.tv_usec = wait_time % 1000 * 1000;
        memcpy(&read_set, &ctx->read_set, sizeof(fd_set));
        memcpy(&write_set, &ctx->write_set, sizeof(fd_set));
        memcpy(&except_set, &ctx->except_set, sizeof(fd_set));
        memset(&res_fd_list, 0, sizeof(res_fd_list));
        nfds = select(0, &read_set, &write_set, &except_set, &tv);
        if (-1 == nfds)
        {
            sw_log_error("%s:%d select: %d", __FILE__, __LINE__, SW_ERRNO);
            return -1;
        }
        if (nfds == 0)
        {
            continue;
        }
        for (i = 0; i < (int)read_set.fd_count; i++)
        {
            fd = read_set.fd_array[i];
            push_to_result_fd_list(&res_fd_list, fd, SW_EV_READ);
        }
        for (i = 0; i < (int)write_set.fd_count; i++)
        {
            fd = write_set.fd_array[i];
            push_to_result_fd_list(&res_fd_list, fd, SW_EV_WRITE);
        }
        for (i = 0; i < (int)except_set.fd_count; i++)
        {
            fd = except_set.fd_array[i];
            push_to_result_fd_list(&res_fd_list, fd, SW_EV_WRITE);
        }
        for (i = 0; i < res_fd_list.fd_count; ++i)
        {
            fd = res_fd_list.fds[i];
            ioevent = &ctx->io_events[fd];
            if (NULL != ioevent->callback)
            {
                ioevent->callback(fd, res_fd_list.events[i], ioevent->arg);
            }
        }
        for (i = 0; i < ctx->checks_count; ++i)
        {
            if (NULL != ctx->checks[i]->callback)
            {
                ctx->checks[i]->callback(ctx->checks[i]->arg);
            }
        }
    }
    return 0;
}
#elif defined(__APPLE__) || defined(__FreeBSD__)
int
sw_ev_io_add(sw_ev_context_t *ctx, int fd, int what_events,
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
    struct kevent kev;
    if (what_events & SW_EV_READ)
    {
        EV_SET(&kev, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
        if (-1 == kevent(ctx->kqueue_fd, &kev, 1, NULL, 0, NULL))
        {
            sw_log_error("%s:%d kevent: %d", __FILE__, __LINE__, SW_ERRNO);
            return -1;
        }
    }
    if (what_events & SW_EV_WRITE)
    {
        EV_SET(&kev, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
        if (-1 == kevent(ctx->kqueue_fd, &kev, 1, NULL, 0, NULL))
        {
            sw_log_error("%s:%d kevent: %d", __FILE__, __LINE__, SW_ERRNO);
            return -1;
        }
    }
    sw_ev_io_t *ioevent = &ctx->io_events[fd];
    ioevent->events |= what_events;
    ioevent->callback = callback;
    ioevent->arg = arg;
    return 0;
}

int
sw_ev_io_del(sw_ev_context_t *ctx, int fd, int what_events)
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
    struct kevent kev;
    if (what_events & SW_EV_READ)
    {
        EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(ctx->kqueue_fd, &kev, 1, NULL, 0, NULL);
    }
    if (what_events & SW_EV_WRITE)
    {
        EV_SET(&kev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        kevent(ctx->kqueue_fd, &kev, 1, NULL, 0, NULL);
    }
    sw_ev_io_t *ioevent = &ctx->io_events[fd];
    if (!ioevent->events)
    {
        return 0;
    }
    ioevent->events = ~what_events & ioevent->events;;
    if (!ioevent->events)
    {
        ioevent->callback = NULL;
        ioevent->arg = NULL;
    }
    return 0;
}

int
sw_ev_loop(sw_ev_context_t *ctx)
{
    struct kevent ready_events[1024];
    int nfds = 0;
    int i = 0;
    int wait_time = -1;
    struct timespec timeout;

    while (ctx->running)
    {
        ctx->current_time = sw_ev_gettime_ms();
        wait_time = process_timers_(ctx);
        for (i = 0; i < ctx->prepares_count; ++i)
        {
            if (NULL != ctx->prepares[i]->callback)
            {
                ctx->prepares[i]->callback(ctx->prepares[i]->arg);
            }
        }
        timeout.tv_sec = wait_time / 1000;
        timeout.tv_nsec = wait_time % 1000 * 1000000;
        nfds = kevent(ctx->kqueue_fd, NULL, 0, ready_events, sizeof(ready_events)/sizeof(struct kevent), &timeout);
        if (nfds == -1)
        {
            if (SW_ERRNO != EINTR)
            {
                sw_log_error("%s:%d kevent: %d", __FILE__, __LINE__, SW_ERRNO);
                return -1;
            }
        }
        for (i = 0; i < nfds; i++)
        {
            int ev_fd = ready_events[i].ident;
            sw_ev_io_t *ioevent = &ctx->io_events[ev_fd];
            int what_events = 0;
            if (ready_events[i].filter == EVFILT_READ)
            {
                what_events |= SW_EV_READ;
            }
            if (ready_events[i].filter & EVFILT_WRITE)
            {
                what_events |= SW_EV_WRITE;
            }
            if (what_events && NULL != ioevent->callback)
            {
                ioevent->callback(ev_fd, what_events, ioevent->arg);
            }
        }
        for (i = 0; i < ctx->checks_count; ++i)
        {
            if (NULL != ctx->checks[i]->callback)
            {
                ctx->checks[i]->callback(ctx->checks[i]->arg);
            }
        }
    }
    return 0;
}

#else /* linux */
int
sw_ev_io_add(sw_ev_context_t *ctx, int fd, int what_events,
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
        sw_log_error("%s:%d epoll_ctl: %d", __FILE__, __LINE__, SW_ERRNO);
        return -1;
    }
    ioevent->events = now_care_what_events;
    ioevent->callback = callback;
    ioevent->arg = arg;
    return 0;
}

int
sw_ev_io_del(sw_ev_context_t *ctx, int fd, int what_events)
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
        sw_log_error("%s:%d epoll_ctl: %d", __FILE__, __LINE__, SW_ERRNO);
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

int
sw_ev_loop(sw_ev_context_t *ctx)
{
    struct epoll_event ready_events[4096];
    int nfds = 0;
    int i = 0;
    int wait_time = -1;
    while (ctx->running)
    {
        ctx->current_time = sw_ev_gettime_ms();
        wait_time = process_timers_(ctx);
        for (i = 0; i < ctx->prepares_count; ++i)
        {
            if (NULL != ctx->prepares[i]->callback)
            {
                ctx->prepares[i]->callback(ctx->prepares[i]->arg);
            }
        }
        nfds = epoll_wait(ctx->epoll_fd, ready_events, sizeof(ready_events)/sizeof(struct epoll_event),  wait_time);
        if (nfds == -1)
        {
            if (SW_ERRNO != EINTR)
            {
                sw_log_error("%s:%d epoll_wait: %d", __FILE__, __LINE__, SW_ERRNO);
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
        for (i = 0; i < ctx->checks_count; ++i)
        {
            if (NULL != ctx->checks[i]->callback)
            {
                ctx->checks[i]->callback(ctx->checks[i]->arg);
            }
        }
    }
    return 0;
}
#endif /* _WIN32 */

sw_ev_timer_t * 
sw_ev_timer_add(sw_ev_context_t *ctx, int timeout_ms,
                void (*callback)(void *arg),
                void *arg)
{
    if (timeout_ms <= 0)
    {
        return NULL;
    }
    sw_ev_timer_t *timer = sw_ev_malloc(sizeof(sw_ev_timer_t));
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
sw_ev_timer_del(sw_ev_context_t *ctx, sw_ev_timer_t *timer)
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
sw_ev_signal_add(sw_ev_context_t *ctx, int sig_no,
                 void (*callback)(int sig_no, void *arg),
                 void *arg)
{
    if (sig_no < 0 || sig_no >= SW_EV_NSIG)
    {
        return -1;
    }
    if (!CAS(&sw_ev_current_signal_context, NULL, ctx))
    {
        if (sw_ev_current_signal_context != ctx)
        {
            sw_log_error("%s:%d sw_ev_signal_add: Please register signal handler in "
                         "the same event context.", 
                         __FILE__, __LINE__);
            return -1;
        }
    }
    signal(sig_no, sw_ev_signal_handler_);
    ctx->signal_events[sig_no].callback = callback;
    ctx->signal_events[sig_no].arg = arg;
    return 0;
}

int
sw_ev_signal_del(sw_ev_context_t *ctx, int sig_no)
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
        sw_log_error("%s:%d sw_ev_signal_del: Please register and unregister signal handler in "
                     "the same event context.", 
                     __FILE__, __LINE__);
        return -1;
    }
    signal(sig_no, SIG_DFL);
    ctx->signal_events[sig_no].callback = NULL;
    ctx->signal_events[sig_no].arg = NULL;
    return 0;
}

sw_ev_prepare_t *
sw_ev_prepare_add(sw_ev_context_t *ctx,
                  void (*callback)(void* arg),
                  void *arg)
{
    if (ctx->prepares_count >= SW_EV_MAX_PREPARE)
    {
        return NULL;
    }
    sw_ev_prepare_t * prepare = sw_ev_malloc(sizeof(sw_ev_prepare_t));
    if (NULL == prepare)
    {
        return NULL;
    }
    prepare->callback = callback;
    prepare->arg = arg;
    ctx->prepares[ctx->prepares_count++] = prepare;
    return prepare;
}

void sw_ev_prepare_del(sw_ev_context_t *ctx, sw_ev_prepare_t *prepare)
{
    if (NULL != prepare)
    {
        int i = 0;
        int found = 0;
        for (; i < ctx->prepares_count; ++i)
        {
            if (ctx->prepares[i] == prepare)
            {
                sw_ev_free(ctx->prepares[i]);
                found = 1;
                break;
            }
        }
        for (; i < ctx->prepares_count - 1; ++i)
        {
            ctx->prepares[i] = ctx->prepares[i+1];
        }
        if (found)
        {
            ctx->prepares[ctx->prepares_count - 1] = NULL;
            --ctx->prepares_count;
        }
    }
}

sw_ev_check_t *
sw_ev_check_add(sw_ev_context_t *ctx,
                void (*callback)(void* arg),
                void *arg)
{
    if (ctx->checks_count >= SW_EV_MAX_CHECK)
    {
        return NULL;
    }
    sw_ev_check_t * check = sw_ev_malloc(sizeof(sw_ev_check_t));
    if (NULL == check)
    {
        return NULL;
    }
    check->callback = callback;
    check->arg = arg;
    ctx->checks[ctx->checks_count++] = check;
    return check;

}

void sw_ev_check_del(sw_ev_context_t *ctx, sw_ev_check_t *check)
{
    if (NULL != check)
    {
        int i = 0;
        int found = 0;
        for (; i < ctx->checks_count; ++i)
        {
            if (ctx->checks[i] == check)
            {
                sw_ev_free(ctx->checks[i]);
                found = 1;
                break;
            }
        }
        for (; i < ctx->checks_count - 1; ++i)
        {
            ctx->checks[i] = ctx->checks[i+1];
        }
        if (found)
        {
            ctx->checks[ctx->checks_count - 1] = NULL;
            --ctx->checks_count;
        }
    }
}

void
sw_ev_loop_exit(sw_ev_context_t *ctx)
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


