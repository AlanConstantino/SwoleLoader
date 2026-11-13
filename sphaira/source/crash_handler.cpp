// Writes a crash report to the log when the app crashes, then lets the system
// handle the crash as it normally would.
//
// Addresses inside the app are written as "nro+0x...". They are offsets into
// the release's symbols .elf; tools/symbolize.sh turns them into functions
// and source lines.
#include "log.hpp"

#include <switch.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace {

// Formats into a fixed buffer: the heap may be locked by the crashed thread.
struct Report {
    char text[16 * 1024];
    size_t size{};

    void Add(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (size >= sizeof(text) - 1) {
            return;
        }
        std::va_list v;
        va_start(v, fmt);
        const auto n = std::vsnprintf(text + size, sizeof(text) - size, fmt, v);
        va_end(v);
        if (n > 0) {
            size = std::min(size + static_cast<size_t>(n), sizeof(text) - 1);
        }
    }
};

Report g_report;

const char* Describe(u32 desc) {
    switch (desc) {
        case ThreadExceptionDesc_InstructionAbort: return "instruction abort";
        case ThreadExceptionDesc_MisalignedPC: return "misaligned program counter";
        case ThreadExceptionDesc_MisalignedSP: return "misaligned stack pointer";
        case ThreadExceptionDesc_SError: return "system error";
        case ThreadExceptionDesc_BadSVC: return "bad system call";
        case ThreadExceptionDesc_Trap: return "trap";
        case ThreadExceptionDesc_Other: return "data abort or other";
        default: return "unknown";
    }
}

struct Region {
    u64 start{};
    u64 end{};
};

Region g_text;

// The app's code is one executable region; this function is inside it.
Region FindText() {
    MemoryInfo info{};
    u32 page{};
    if (R_FAILED(svcQueryMemory(&info, &page, reinterpret_cast<u64>(&FindText)))) {
        return {};
    }
    return {info.addr, info.addr + info.size};
}

bool Readable(u64 address, u64 size) {
    MemoryInfo info{};
    u32 page{};
    return R_SUCCEEDED(svcQueryMemory(&info, &page, address))
        && info.type != MemType_Unmapped && (info.perm & Perm_R)
        && address >= info.addr && address + size <= info.addr + info.size;
}

void AddAddress(const char* name, u64 address) {
    if (address >= g_text.start && address < g_text.end) {
        g_report.Add("  %-4s nro+0x%lx\n", name, address - g_text.start);
    } else {
        g_report.Add("  %-4s 0x%016lx\n", name, address);
    }
}

} // namespace

extern "C" {

// The default exception stack is 1 KiB, too small for formatting.
alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    g_text = FindText();
    g_report.size = 0;

    g_report.Add("\n[CRASH] " APP_NAME " " APP_DISPLAY_VERSION " crashed: %s (0x%x)\n", Describe(ctx->error_desc), ctx->error_desc);
    g_report.Add("  symbolize with: tools/symbolize.sh <symbols .elf for this version> crash.txt\n");
    AddAddress("pc", ctx->pc.x);
    AddAddress("lr", ctx->lr.x);
    g_report.Add("  sp   0x%016lx\n  fp   0x%016lx\n  far  0x%016lx\n  esr  0x%08x\n", ctx->sp.x, ctx->fp.x, ctx->far.x, ctx->esr);
    g_report.Add("  code 0x%016lx - 0x%016lx\n", g_text.start, g_text.end);

    // Frame records are two words, the caller's frame pointer and the return
    // address. Each is checked to be readable, as a fault here would end the
    // process without a report.
    g_report.Add("backtrace:\n");
    u64 fp = ctx->fp.x;
    for (int i = 0; i < 32 && fp && !(fp & 0xF) && Readable(fp, 16); ++i) {
        const auto frame = reinterpret_cast<const u64*>(fp);
        if (!frame[1]) {
            break;
        }
        char name[8];
        std::snprintf(name, sizeof(name), "#%d", i);
        AddAddress(name, frame[1]);
        if (frame[0] <= fp) {
            break;
        }
        fp = frame[0];
    }

    g_report.Add("registers:\n");
    for (int i = 0; i < 29; i += 2) {
        if (i + 1 < 29) {
            g_report.Add("  x%-2d 0x%016lx  x%-2d 0x%016lx\n", i, ctx->cpu_gprs[i].x, i + 1, ctx->cpu_gprs[i + 1].x);
        } else {
            g_report.Add("  x%-2d 0x%016lx\n", i, ctx->cpu_gprs[i].x);
        }
    }

    log_write_crash(g_report.text, g_report.size);
    // returning hands the crash back to the system.
}

} // extern "C"
