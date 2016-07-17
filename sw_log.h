#ifndef INC_SW_LOG_H
#define INC_SW_LOG_H

#ifdef __cplusplus
extern "C"
{
#endif

void sw_log_error_exit(const char *fmt, ...);
void sw_log_error(const char *fmt, ...);
void sw_log_warn(const char *fmt, ...);
void sw_log_msg(const char *fmt, ...);
void sw_log_debug(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
