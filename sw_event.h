/**
 * libsmallwater_event is a light weight net event library.
 * Support events : socket read write, timer, signal, prepare, check.
 * Similar to libevent, redesign a event library just because we want
 * more simple to use, more efficient and less memory.
 * Currently supporting platform: linux
 * Copyright (c) 2016 ShuishengWu <shuisheng918@gmail.com>
 */
#ifndef INC_SW_EVENT_H
#define INC_SW_EVENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum /* event type */
{
    SW_EV_READ    = 0x01, /* read ready event */
    SW_EV_WRITE   = 0x02, /* write ready event */
};

/**
 * Alloc and initialize a sw_ev_context, then return it.
 * return:  NULL failed, else success.
 * note:    sw_ev_context is not thread safe, so you can't operate it in different thread.
 *          If you want use sw_ev_context in multi-thread program, you should offer different 
 *          sw_ev_context to different thread, one sw_ev_context to one thread. You should 
 *          destroy sw_ev_context with sw_ev_context_free() api prevent memeory leak when
 *          you needn't it. 
 */
struct sw_ev_context * sw_ev_context_new();

/**
 * Destroy and free the sw_ev_context.
 * param:   ctx - sw_ev_context you want destroy.
 */
void sw_ev_context_free(struct sw_ev_context *ctx);

/**
 * Add a socket io event to ctx.
 * param:   ctx - operated sw_ev_context pointer which return by sw_ev_context_new().
 *          fd - socket handle(file descriptor).
 *          what_events - The bits or of SW_EV_READ and SW_EV_WRITE.
 *          callback - It will be called when events readied. callback's first argument is the
 *          socket which have events readied, second argument offer what events readied, third
 *          argument is the user data pointer.
 *          arg - user data pointer.
 * return:  0 success, -1 failed.
 * note:    Socket's read and write event share the same callback function and user data pointer.
 *          You should make the socket nonblock.
 */
int  sw_ev_io_add(struct sw_ev_context *ctx, int fd, int what_events,
                  void (*callback)(int fd, int events, void *arg),
                  void *arg);

/**
 * Delete a socket io event from the ctx.
 * param:  ctx - operated sw_ev_context pointer which return by sw_ev_context_new().
 *         fd - socket handle(file descriptor).
 *         what_events - The bits or of SW_EV_READ and SW_EV_WRITE.
 * return:  0 success, -1 failed.
 */
int  sw_ev_io_del(struct sw_ev_context *ctx, int fd, int what_events);

/**
 * Add a timer event to the ctx.
 * Alloc and init a sw_ev_timer struct. When the timer is expired, callback will be called,
 * then rescheduled this timer. So next timeout_ms expired, callback still be called until
 * you use sw_ev_timer_del() explictly.
 * param:   ctx - operated sw_ev_context pointer which return by sw_ev_context_new().
 *          callback - It will be called when timer event readied. callback's argument is the
 *          the user data pointer.
 *          arg - user data pointer.
 * return:  not NULL success, NULL failed.
 * note:    You must use sw_ev_timer_del() to stop timer and prevent memory leak.
 */
struct sw_ev_timer * 
sw_ev_timer_add(struct sw_ev_context *ctx, int timeout_ms,
                void (*callback)(void *arg),
                void *arg);

/**
 * Delete the timer event from ctx.
 * param:   ctx - operated sw_ev_context pointer which return by sw_ev_context_new().
 *          timer - timer pointer returned by sw_ev_timer_add().
 * return:  0 success, -1 failed.
 */
int  sw_ev_timer_del(struct sw_ev_context *ctx, struct sw_ev_timer *timer);

/**
 * Add a signal event to ctx.
 * We just support add or delete signal event in the same sw_ev_context. Once you add
 * a signal event to the ctx, you must operate signal event in this ctx for subsequently 
 * signal operations. Suggest processing signals in only one thread's sw_ev_context, block signals
 * in other threads when you using this event library in multi-thread environment.
 * param:   ctx - operated sw_ev_context pointer which return by sw_ev_context_new().
 *          sig_no - sinal number, the legal value range is [0, 64).
 *          callback - It will be called when signal reach. callback's first argument is the
 *          signal number, second argument is the user data pointer.
 *          arg - user data pointer.
 * return:  0 success, -1 failed.
 */
int  sw_ev_signal_add(struct sw_ev_context *ctx, int sig_no,
                      void (*callback)(int sig_no, void *arg),
                      void *arg);

/**
 * Delete the signal event from ctx.
 * This signal's handler will be restored to the default(SIG_DFL).
 * param:   ctx - operated sw_ev_context pointer which return by sw_ev_context_new().
 *          sig_no - sinal number, the legal value range is [0, 64).
 * return:  0 success, -1 failed.
 */
int  sw_ev_signal_del(struct sw_ev_context *ctx, int sig_no);

/**
 * Set the prepare function before poll wait.
 * param:   ctx - operated sw_ev_context pointer which return by sw_ev_context_new().
 *          callback - It will be called after timers processed and before io poll 
 *          wait. callback's argument is the user data pointer. It will disable
 *          prepare event when callback is null.
 *          arg - user data pointer.
 */
void sw_ev_prepare_set(struct sw_ev_context *ctx,
                       void (*callback)(void* arg),
                       void *arg);

/**
 * Set the check function after poll wait.
 * param:   ctx - operated sw_ev_context pointer which return by sw_ev_context_new().
 *          callback - It will be called after io poll wait and io events
 *          processed. callback's argument is the user data pointer. It will disable
 *          check event when callback is null.
 *          arg - user data pointer.
 */
void sw_ev_check_set(struct sw_ev_context *ctx,
                     void (*callback)(void* arg),
                     void *arg);

/**
 * Run event loop on the ctx and process events one by one.
 * param:   ctx - operated sw_ev_context pointer which return by sw_ev_context_new().
 */
int  sw_ev_loop(struct sw_ev_context *ctx);

/**
 * Exit the event loop.
 * param:   ctx - operated sw_ev_context pointer which return by sw_ev_context_new().
 */
void sw_ev_loop_exit(struct sw_ev_context *ctx);

/**
 * Set the memeory manager function instead std dynamic memory manager function.
 */
void sw_ev_set_memory_func(void* (*malloc_func)(size_t),
                           void  (*free_func)(void *),
                           void* (*realloc_func)(void *, size_t));


/**
 * log, may be useful for debug.
 */
enum /* log level */
{
    SW_LOG_ERROR = 0,
    SW_LOG_WARN  = 1,
    SW_LOG_MSG   = 2,
    SW_LOG_DEBUG = 3
};
typedef void (*sw_log_func_t)(int log_level, const char * msg);

/**
 * Defaultly, log function is null, the messages will print to stdout.
 * You can set your own log function use this interface.
 */
void sw_set_log_func(sw_log_func_t logfunc);


#ifdef __cplusplus
}
#endif

#endif
