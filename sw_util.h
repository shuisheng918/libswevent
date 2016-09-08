#ifndef INC_SW_UTIL_H
#define INC_SW_UTIL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

int64_t  sw_ev_gettime_ms();
int sw_ev_setnonblock(int fd);
int sw_ev_socketpair(int fd[2]);

#ifdef _WIN32
#define SW_EV_CLOSESOCKET(s) closesocket(s)
#define SW_ERRNO  GetLastError()
#else
#define SW_EV_CLOSESOCKET(s) close(s)
#define SW_ERRNO  errno
#endif

#ifdef _WIN32
#define CAS(ptr, old_val, new_val) \
    InterlockedCompareExchange((volatile LONG *)ptr, (LONG)new_val, (LONG)old_val)
#else
#define CAS(ptr, old_val, new_val) \
    __sync_bool_compare_and_swap(ptr, old_val, new_val)
#endif

#ifdef __cplusplus
}
#endif

#endif