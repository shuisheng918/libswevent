#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "sw_event.h"

extern sw_log_func_t log_func;

static void sw_log(int log_level, const char *fmt, va_list ap)
{
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    log_func(log_level, buf);
}

void sw_log_error_exit(const char *fmt, ...)
{
    if (NULL != log_func)
    {
        va_list ap;
        va_start(ap, fmt);
        sw_log(SW_LOG_ERROR, fmt, ap);
        va_end(ap);
    }
    exit(1);
}

void sw_log_error(const char *fmt, ...)
{
    if (NULL != log_func)
    {
        va_list ap;
        va_start(ap, fmt);
        sw_log(SW_LOG_ERROR, fmt, ap);
        va_end(ap);
    }
}

void sw_log_warn(const char *fmt, ...)
{
    if (NULL != log_func)
    {
        va_list ap;
        va_start(ap, fmt);
        sw_log(SW_LOG_WARN, fmt, ap);
        va_end(ap);
    }
}

void sw_log_msg(const char *fmt, ...)
{
    if (NULL != log_func)
    {
        va_list ap;
        va_start(ap, fmt);
        sw_log(SW_LOG_MSG, fmt, ap);
        va_end(ap);
    }
}

void sw_log_debug(const char *fmt, ...)
{
    if (NULL != log_func)
    {
        va_list ap;
        va_start(ap, fmt);
        sw_log(SW_LOG_DEBUG, fmt, ap);
        va_end(ap);
    }
}


