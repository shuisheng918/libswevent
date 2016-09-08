#ifndef INC_SW_EVENT_INTERNAL_H
#define INC_SW_EVENT_INTERNAL_H

#include <stdint.h>

typedef struct sw_ev_timer
{
    void (*callback)(void *arg);
    void *arg;
    int64_t next_expire_time;  /* ms */
    unsigned  index_in_heap; 
    int       interval;  /* ms */
} sw_ev_timer_t;

typedef struct sw_ev_io
{
    void (*callback)(int fd, int events, void *arg);
    void *arg;
    int  events;
} sw_ev_io_t;

typedef struct sw_ev_signal
{
    void (*callback)(int sig_no, void *arg);
    void *arg;
} sw_ev_signal_t;

typedef struct sw_ev_prepare
{
    void (*callback)(void* arg);
    void *arg;
} sw_ev_prepare_t;

typedef struct sw_ev_check
{
    void (*callback)(void* arg);
    void *arg;
} sw_ev_check_t;

typedef struct sw_ev_context
{
    int64_t  current_time; /* ms */
    int      running;
#ifdef _WIN32
    fd_set	read_set;
    fd_set	write_set;
    fd_set	except_set;
#elif defined(__APPLE__) || defined(__FreeBSD__)
    int 	kqueue_fd;
#elif defined(__linux__)
    int     epoll_fd;
#else
#error Not support current operating system yet.
#endif
    sw_ev_io_t * io_events;
    int          io_events_count;
    struct sw_timer_heap * timer_heap;
    sw_ev_prepare_t   prepare;
    sw_ev_check_t     check;
    int               signal_pipe[2];
    sw_ev_signal_t   *signal_events; /* elements count: NSIG */
} sw_ev_context_t;

#endif
