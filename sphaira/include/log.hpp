#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define sphaira_USE_LOG 1

// log.txt is this session, previous.txt the one before, crash.txt the last
// crash report.
#define LOG_DIRECTORY APP_CONFIG_DIR "/logs"

#include <stdarg.h>
#include <stddef.h>

#if sphaira_USE_LOG
bool log_file_init();
bool log_nxlink_init();
void log_file_exit();
bool log_is_init();
// writes buffered lines to the log file now.
void log_flush();
// for the exception handler only. see log.cpp.
void log_write_crash(const char* report, size_t size);

void log_nxlink_exit();
void log_write(const char* s, ...) __attribute__ ((format (printf, 1, 2)));
void log_write_arg(const char* s, va_list* v);
#else
inline bool log_file_init() {
    return true;
}
inline bool log_nxlink_init() {
    return true;
}
#define log_file_exit()
#define log_flush()
#define log_write_crash(...)
#define log_nxlink_exit()
#define log_write(...)
#define log_write_arg(...)
#endif

#ifdef __cplusplus
}
#endif
