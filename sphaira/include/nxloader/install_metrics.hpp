#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace nxloader {

struct InstallMetrics {
    bool measured{};
    std::uint64_t elapsed_ns{};
    std::uint64_t source_bytes{};
    std::uint64_t written_bytes{};
    std::uint64_t source_read_ns{};
    std::uint64_t destination_write_ns{};
    std::uint64_t processing_wall_ns{};
    std::uint64_t read_wait_ns{};
    std::uint64_t process_input_wait_ns{};
    std::uint64_t process_output_wait_ns{};
    std::uint64_t write_wait_ns{};
    std::uint64_t write_pacing_ns{};
    std::uint64_t contents_written{};
    std::uint64_t contents_reused{};
    std::uint64_t titles_skipped{};

    auto ProcessingActiveNs() const -> std::uint64_t {
        const auto waiting = process_input_wait_ns + process_output_wait_ns;
        return processing_wall_ns > waiting ? processing_wall_ns - waiting : 0;
    }

    void AddPipeline(const InstallMetrics& other) {
        source_bytes += other.source_bytes;
        written_bytes += other.written_bytes;
        source_read_ns += other.source_read_ns;
        destination_write_ns += other.destination_write_ns;
        processing_wall_ns += other.processing_wall_ns;
        read_wait_ns += other.read_wait_ns;
        process_input_wait_ns += other.process_input_wait_ns;
        process_output_wait_ns += other.process_output_wait_ns;
        write_wait_ns += other.write_wait_ns;
        write_pacing_ns += other.write_pacing_ns;
    }
};

inline std::string FormatInstallMetrics(const InstallMetrics& metrics) {
    char text[1024];
    const auto seconds = [](std::uint64_t ns) { return double(ns) / 1e9; };
    if (!metrics.measured) {
        std::snprintf(text, sizeof(text), "  Elapsed: %.3f s; content pipeline metrics unavailable.\n",
            seconds(metrics.elapsed_ns));
        return text;
    }

    const auto input_mib = double(metrics.source_bytes) / (1024.0 * 1024.0);
    const auto output_mib = double(metrics.written_bytes) / (1024.0 * 1024.0);
    const auto elapsed = seconds(metrics.elapsed_ns);
    std::snprintf(text, sizeof(text),
        "  Elapsed: %.3f s; content input: %.2f MiB; content output accepted: %.2f MiB.\n"
        "  Package averages: input %.2f MiB/s; output %.2f MiB/s.\n"
        "  Read calls: %.3f s; processing active: %.3f s; write calls: %.3f s.\n"
        "  Queue waits: reader %.3f s; processor input %.3f s / output %.3f s; writer %.3f s.\n"
        "  Write pacing: %.3f s; content written: %llu; content reused: %llu; titles skipped: %llu.\n",
        elapsed, input_mib, output_mib, elapsed > 0 ? input_mib / elapsed : 0,
        elapsed > 0 ? output_mib / elapsed : 0,
        seconds(metrics.source_read_ns), seconds(metrics.ProcessingActiveNs()), seconds(metrics.destination_write_ns),
        seconds(metrics.read_wait_ns), seconds(metrics.process_input_wait_ns),
        seconds(metrics.process_output_wait_ns), seconds(metrics.write_wait_ns), seconds(metrics.write_pacing_ns),
        static_cast<unsigned long long>(metrics.contents_written),
        static_cast<unsigned long long>(metrics.contents_reused),
        static_cast<unsigned long long>(metrics.titles_skipped));
    return text;
}

} // namespace nxloader
