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
    const char * level_str = "--?--";
    if (NULL != log_func)
    {
        log_func(log_level, buf);
    }
    else
    {
        switch (log_level)
        {
        case SW_LOG_ERROR:
            level_str = "ERROR";
            break;
        case SW_LOG_WARN:
            level_str = "WARN";
            break;
        case SW_LOG_MSG:
            level_str = "MSG";
            break;
        case SW_LOG_DEBUG:
            level_str = "DEBUG";
            break;
        default:
            break; 
        }
        fprintf(stdout, "[%s] %s\n", level_str, buf);
    }
}

void sw_log_error_exit(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sw_log(SW_LOG_ERROR, fmt, ap);
    va_end(ap);
    exit(1);
}

void sw_log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sw_log(SW_LOG_ERROR, fmt, ap);
    va_end(ap);
}

void sw_log_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sw_log(SW_LOG_WARN, fmt, ap);
    va_end(ap);
}

void sw_log_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sw_log(SW_LOG_MSG, fmt, ap);
    va_end(ap);
}

void sw_log_debug(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sw_log(SW_LOG_DEBUG, fmt, ap);
    va_end(ap);
}


