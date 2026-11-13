#include "log.hpp"
#include "defines.hpp"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <atomic>
#include <string>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <switch.h>

#if sphaira_USE_LOG
namespace {

constexpr const char* LOG_DIR = LOG_DIRECTORY;
constexpr const char* LOG_PATH = LOG_DIRECTORY "/log.txt";
constexpr const char* PREVIOUS_LOG_PATH = LOG_DIRECTORY "/previous.txt";
constexpr const char* CRASH_PATH = LOG_DIRECTORY "/crash.txt";

// lines are buffered in memory and written in batches, as opening the file
// for every line slowed down installs that log per block.
constexpr std::size_t FLUSH_BYTES = 32 * 1024;
constexpr u64 FLUSH_INTERVAL_NS = 1'000'000'000;
// once the current file passes this it becomes previous.txt and a new one is
// started, so the newest lines are always kept and the sd card use is bounded.
constexpr long MAX_LOG_BYTES = 4 * 1024 * 1024;

std::atomic_int32_t nxlink_socket{};
std::atomic_bool g_file_open{};
// guards g_buffer.
Mutex g_mutex;
// serialises writes to the file, held across taking the buffer so batches
// land in order.
Mutex g_write_mutex;
std::string g_buffer;
long g_file_size{};

Thread g_flush_thread;
UEvent g_flush_exit;
bool g_flush_thread_running{};

void CreateLogDir() {
    mkdir("/config", 0777);
    mkdir(APP_CONFIG_DIR, 0777);
    mkdir(LOG_DIR, 0777);
}

void RotateLocked() {
    std::remove(PREVIOUS_LOG_PATH);
    std::rename(LOG_PATH, PREVIOUS_LOG_PATH);
    g_file_size = 0;
}

void AppendLocked(const char* data, std::size_t size) {
    if (!size) {
        return;
    }
    if (g_file_size + static_cast<long>(size) > MAX_LOG_BYTES) {
        RotateLocked();
        static constexpr char header[] = "(log continued: earlier lines of this session are in previous.txt)\n";
        if (auto file = std::fopen(LOG_PATH, "w")) {
            std::fwrite(header, 1, sizeof(header) - 1, file);
            std::fclose(file);
            g_file_size = sizeof(header) - 1;
        }
    }
    if (auto file = std::fopen(LOG_PATH, "a")) {
        g_file_size += std::fwrite(data, 1, size, file);
        std::fclose(file);
    }
}

void Flush() {
    SCOPED_MUTEX(&g_write_mutex);
    std::string pending;
    {
        SCOPED_MUTEX(&g_mutex);
        pending.swap(g_buffer);
    }
    if (g_file_open) {
        AppendLocked(pending.data(), pending.size());
    }
}

void FlushThreadFunc(void*) {
    while (R_FAILED(waitSingle(waiterForUEvent(&g_flush_exit), FLUSH_INTERVAL_NS))) {
        Flush();
    }
    Flush();
}

void StartFlushThread() {
    if (g_flush_thread_running) {
        return;
    }
    ueventCreate(&g_flush_exit, false);
    // low priority, it only wakes once a second.
    if (R_SUCCEEDED(threadCreate(&g_flush_thread, FlushThreadFunc, nullptr, nullptr, 16 * 1024, 0x3B, -2))) {
        if (R_SUCCEEDED(threadStart(&g_flush_thread))) {
            g_flush_thread_running = true;
        } else {
            threadClose(&g_flush_thread);
        }
    }
}

void StopFlushThread() {
    if (!g_flush_thread_running) {
        return;
    }
    ueventSignal(&g_flush_exit);
    threadWaitForExit(&g_flush_thread);
    threadClose(&g_flush_thread);
    g_flush_thread_running = false;
}

// failures are written straight away, in case they are followed by a crash.
bool IsUrgent(const char* text) {
    for (auto p = text; *p; ++p) {
        if (!strncasecmp(p, "error", 5) || !strncasecmp(p, "fail", 4) || !strncmp(p, "[CRASH]", 7)) {
            return true;
        }
    }
    return false;
}

void log_write_arg_internal(const char* s, std::va_list* v) {
    // to the millisecond: the log is how latency gets measured on the console.
    timeval tv{};
    gettimeofday(&tv, nullptr);
    const auto t = static_cast<std::time_t>(tv.tv_sec);
    std::tm tm{};
    localtime_r(&t, &tm);

    char buf[1024];
    const auto prefix = std::snprintf(buf, sizeof(buf), "[%02u:%02u:%02u.%03u] -> ", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<unsigned>(tv.tv_usec / 1000));
    const auto body = std::vsnprintf(buf + prefix, sizeof(buf) - prefix, s, *v);
    auto len = static_cast<std::size_t>(prefix + (body < 0 ? 0 : body));
    if (len >= sizeof(buf)) {
        // long lines are cut, but still end the line.
        len = sizeof(buf) - 1;
        buf[len - 1] = '\n';
    }

    const bool urgent = IsUrgent(buf + prefix);
    bool flush{};
    {
        SCOPED_MUTEX(&g_mutex);
        if (g_file_open) {
            g_buffer.append(buf, len);
            // the flush thread handles the once a second writes, which keeps
            // file access off the ui and install threads.
            flush = urgent || g_buffer.size() >= FLUSH_BYTES;
        }
        if (nxlink_socket) {
            std::printf("%s", buf);
        }
    }

    if (flush) {
        Flush();
    }
}

} // namespace

extern "C" {

auto log_file_init() -> bool {
    SCOPED_MUTEX(&g_write_mutex);
    if (g_file_open) {
        return false;
    }

    // keep the last session's log, it is the one that explains a crash.
    CreateLogDir();
    RotateLocked();
    auto file = std::fopen(LOG_PATH, "w");
    if (!file) {
        return false;
    }
    std::fclose(file);

    {
        SCOPED_MUTEX(&g_mutex);
        g_buffer.clear();
        g_buffer.reserve(FLUSH_BYTES * 2);
        g_file_open = true;
    }
    StartFlushThread();
    return true;
}

auto log_nxlink_init() -> bool {
    SCOPED_MUTEX(&g_mutex);
    if (nxlink_socket) {
        return false;
    }

    // -1 when the app wasn't launched over nxlink; kept, every log line would be
    // formatted and written to nowhere.
    const auto sock = nxlinkConnectToHost(true, false);
    if (sock < 0) {
        return false;
    }

    // every line is a small write that the next one waits on, so nagle leaves each
    // sitting on the host's delayed ack and skews the timings being measured.
    const int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    nxlink_socket = sock;
    return true;
}

void log_file_exit() {
    StopFlushThread();
    Flush();
    SCOPED_MUTEX(&g_write_mutex);
    g_file_open = false;
}

void log_flush() {
    if (g_file_open) {
        Flush();
    }
}

void log_nxlink_exit() {
    SCOPED_MUTEX(&g_mutex);
    if (nxlink_socket) {
        close(nxlink_socket);
        nxlink_socket = 0;
    }
}

bool log_is_init() {
    return g_file_open || nxlink_socket;
}

void log_write(const char* s, ...) {
    if (!log_is_init()) {
        return;
    }

    std::va_list v{};
    va_start(v, s);
    log_write_arg_internal(s, &v);
    va_end(v);
}

void log_write_arg(const char* s, va_list* v) {
    if (!log_is_init()) {
        return;
    }

    log_write_arg_internal(s, v);
}

// Called from the exception handler, where the crashed thread may hold any
// lock, including the heap's. So this takes the log locks only if they come
// free quickly, and writes through the sd card's filesystem directly instead
// of newlib, which allocates.
void log_write_crash(const char* report, size_t size) {
    bool locked_write{}, locked_buffer{};
    for (int i = 0; i < 50 && !(locked_write = mutexTryLock(&g_write_mutex)); ++i) {
        svcSleepThread(1'000'000);
    }
    for (int i = 0; i < 50 && !(locked_buffer = mutexTryLock(&g_mutex)); ++i) {
        svcSleepThread(1'000'000);
    }

    if (auto fs = fsdevGetDeviceFileSystem("sdmc")) {
        // logging may never have been enabled, so the folder may be missing.
        fsFsCreateDirectory(fs, "/config");
        fsFsCreateDirectory(fs, APP_CONFIG_DIR);
        fsFsCreateDirectory(fs, LOG_DIRECTORY);

        const auto write = [fs](const char* path, bool append, const char* a, size_t a_size, const char* b, size_t b_size) {
            char fs_path[FS_MAX_PATH]{};
            std::strncpy(fs_path, path, sizeof(fs_path) - 1);
            if (!append) {
                fsFsDeleteFile(fs, fs_path);
            }
            fsFsCreateFile(fs, fs_path, 0, 0);
            FsFile file;
            if (R_FAILED(fsFsOpenFile(fs, fs_path, FsOpenMode_Write | FsOpenMode_Append, &file))) {
                return;
            }
            s64 offset{};
            fsFileGetSize(&file, &offset);
            if (a_size && R_SUCCEEDED(fsFileWrite(&file, offset, a, a_size, FsWriteOption_None))) {
                offset += a_size;
            }
            if (b_size) {
                fsFileWrite(&file, offset, b, b_size, FsWriteOption_None);
            }
            fsFileFlush(&file);
            fsFileClose(&file);
        };

        // the buffered lines lead up to the crash, so they go first.
        if (g_file_open) {
            write(LOG_PATH, true, g_buffer.data(), g_buffer.size(), report, size);
            g_buffer.clear();
        }
        write(CRASH_PATH, false, report, size, nullptr, 0);
    }

    if (nxlink_socket) {
        send(nxlink_socket, report, size, 0);
    }

    if (locked_buffer) {
        mutexUnlock(&g_mutex);
    }
    if (locked_write) {
        mutexUnlock(&g_write_mutex);
    }
}

} // extern "C"

#endif
