#include "nxloader/install_metrics.hpp"

#include <cassert>
#include <iostream>

int main() {
    nxloader::InstallMetrics metrics;
    assert(nxloader::FormatInstallMetrics(metrics).find("unavailable") != std::string::npos);

    metrics.measured = true;
    auto report = nxloader::FormatInstallMetrics(metrics);
    assert(report.find("input 0.00 MiB/s; output 0.00 MiB/s") != std::string::npos);
    assert(report.find("inf") == std::string::npos && report.find("nan") == std::string::npos);

    metrics.elapsed_ns = 2'000'000'000;
    metrics.source_bytes = 1024 * 1024;
    metrics.written_bytes = 4 * 1024 * 1024;
    metrics.processing_wall_ns = 1'900'000'000;
    metrics.process_input_wait_ns = 400'000'000;
    metrics.process_output_wait_ns = 600'000'000;
    metrics.contents_reused = 7;
    metrics.titles_skipped = 2;
    assert(metrics.ProcessingActiveNs() == 900'000'000);
    report = nxloader::FormatInstallMetrics(metrics);
    assert(report.find("input 0.50 MiB/s; output 2.00 MiB/s") != std::string::npos);
    assert(report.find("content output accepted: 4.00 MiB") != std::string::npos);
    assert(report.find("content reused: 7; titles skipped: 2") != std::string::npos);
    assert(report.find("processing active: 0.900 s") != std::string::npos);

    nxloader::InstallMetrics next;
    next.source_bytes = 3 * 1024 * 1024;
    next.written_bytes = 6 * 1024 * 1024;
    next.source_read_ns = 100;
    next.destination_write_ns = 200;
    next.processing_wall_ns = 900;
    next.process_input_wait_ns = 200;
    next.process_output_wait_ns = 300;
    next.read_wait_ns = 400;
    next.write_wait_ns = 500;
    next.write_pacing_ns = 600;
    metrics.AddPipeline(next);
    assert(metrics.source_bytes == 4 * 1024 * 1024);
    assert(metrics.written_bytes == 10 * 1024 * 1024);
    assert(metrics.elapsed_ns == 2'000'000'000);
    assert(metrics.source_read_ns == 100 && metrics.destination_write_ns == 200);
    assert(metrics.read_wait_ns == 400 && metrics.write_wait_ns == 500 && metrics.write_pacing_ns == 600);
    assert(metrics.contents_reused == 7 && metrics.titles_skipped == 2);
    assert(metrics.ProcessingActiveNs() == 900'000'400);

    next.processing_wall_ns = 0;
    assert(next.ProcessingActiveNs() == 0);
    std::cout << "Install timing/report tests passed.\n";
}
