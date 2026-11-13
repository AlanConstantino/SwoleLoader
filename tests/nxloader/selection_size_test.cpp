#include "nxloader/selection_size.hpp"
#include "utils/thread.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <map>
#include <limits>
#include <mutex>
#include <thread>

using sphaira::nxloader_ui::SelectionSizer;
using nxloader::EntryKind;
using namespace std::chrono_literals;

struct FakeFs final : fs::SizeFs {
    std::map<std::string, std::vector<nxloader::Entry>> directories;
    std::map<std::string, nxloader::PackageSize> packages;
    std::atomic_size_t reads{}, metadata_reads{}, active{}, peak{};
    std::mutex mutex;
    std::condition_variable condition;
    bool entered{}, released{};
    std::string blocked, failure;
    bool generated{};

    void Enter(const std::string& path) {
        const auto now = ++active;
        peak.store(std::max(peak.load(), now));
        if (path == blocked) {
            std::unique_lock lock(mutex);
            entered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return released; });
        }
    }

    std::string ReadDirectory(const std::string& path, const Emit& emit) override {
        ++reads;
        Enter(path);
        if (path == failure) {
            --active;
            return "drive removed";
        }
        for (const auto& entry : directories[path]) if (!emit(entry)) break;
        --active;
        return {};
    }

    nxloader::PackageSize ReadPackage(const std::string& path) override {
        ++metadata_reads;
        Enter(path);
        const auto result = generated ? nxloader::PackageSize{10, 100, 1, true, {}} : packages.at(path);
        --active;
        return result;
    }

    Result FileGetSizeAndTimestamp(const fs::FsPath&, FsTimeStampRaw*, s64*) override { return 1; }

    void WaitEntered() {
        std::unique_lock lock(mutex);
        assert(condition.wait_for(lock, 5s, [&] { return entered; }));
    }
    void Release() {
        std::lock_guard lock(mutex);
        released = true;
        condition.notify_all();
    }
};

SelectionSizer::Summary Finish(SelectionSizer& sizer, std::uint64_t id) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline) {
        for (const auto& update : sizer.Poll()) {
            assert(update.id == id);
            if (update.summary.complete) return update.summary;
        }
        std::this_thread::sleep_for(1ms);
    }
    assert(false && "size worker did not complete");
    return {};
}

void selected_only_and_cache() {
    auto source = std::make_shared<FakeFs>();
    source->directories["/Game"] = {{"base.nsz", EntryKind::File}, {"DLC", EntryKind::Directory}, {"note.txt", EntryKind::File}};
    source->directories["/Game/DLC"] = {{"extra.nsp", EntryKind::File}};
    source->packages["/Game/base.nsz"] = {10, 100, 3, true, {}};
    source->packages["/Game/DLC/extra.nsp"] = {20, 18, 2, true, {}};
    SelectionSizer sizer(source);
    assert(sizer.IsIdle() && source->reads == 0 && source->metadata_reads == 0);
    const nxloader::Root root{"/Game", EntryKind::Directory};
    sizer.SetSelected(7, root, true);
    auto result = Finish(sizer, 7);
    assert(result.source_bytes == 30 && result.installed_bytes == 118 && result.content_count == 5);
    assert(result.package_count == 2 && result.unknown_count == 0 && result.error.empty());
    sizer.WaitIdle();
    const auto reads = source->metadata_reads.load();
    sizer.SetSelected(7, root, false);
    assert(sizer.Poll().empty());
    sizer.SetSelected(7, root, true);
    result = Finish(sizer, 7);
    assert(result.installed_bytes == 118 && source->metadata_reads == reads);
    assert(source->peak == 1);
}

void exclusions_reuse_catalog_and_unknowns_retry() {
    auto source = std::make_shared<FakeFs>();
    source->directories["/Game"] = {{"base.nsz", EntryKind::File}, {"update.nsz", EntryKind::File}};
    source->packages["/Game/base.nsz"] = {10, 100, 1, true, {}};
    source->packages["/Game/update.nsz"] = {2, 20, 1, true, {}};
    SelectionSizer sizer(source);
    sizer.SetSelected(1, {"/Game", EntryKind::Directory}, true);
    assert(Finish(sizer, 1).installed_bytes == 120);
    sizer.WaitIdle();
    const auto reads = source->metadata_reads.load();
    sizer.SetSelected(1, {"/Game", EntryKind::Directory}, true, {"/Game/base.nsz"});
    assert(Finish(sizer, 1).installed_bytes == 20);
    sizer.WaitIdle();
    sizer.SetSelected(1, {"/Game", EntryKind::Directory}, true);
    assert(Finish(sizer, 1).installed_bytes == 120);
    assert(source->metadata_reads == reads);
    sizer.WaitIdle();
    source->packages["/Game/update.nsz"] = {2, 0, 0, false, "read failed"};
    sizer.SetSelected(1, {"/Game", EntryKind::Directory}, true, {}, true);
    assert(Finish(sizer, 1).unknown_count == 1);
    sizer.WaitIdle();
    source->packages["/Game/update.nsz"] = {2, 30, 1, true, {}};
    sizer.SetSelected(1, {"/Game", EntryKind::Directory}, false);
    sizer.SetSelected(1, {"/Game", EntryKind::Directory}, true);
    assert(Finish(sizer, 1).installed_bytes == 130);
}

void cancel_and_navigation_reject_stale_results() {
    auto source = std::make_shared<FakeFs>();
    source->blocked = "/old.nsz";
    source->packages["/old.nsz"] = {1, 1000, 1, true, {}};
    source->packages["/new.nsz"] = {2, 2000, 1, true, {}};
    SelectionSizer sizer(source);
    sizer.SetSelected(0, {"/old.nsz", EntryKind::File}, true);
    source->WaitEntered();
    sizer.Cancel();
    auto wait = std::async(std::launch::async, [&] { sizer.WaitIdle(); });
    assert(wait.wait_for(20ms) == std::future_status::timeout);
    sizer.Clear();
    sizer.SetSelected(0, {"/new.nsz", EntryKind::File}, true);
    source->Release();
    const auto result = Finish(sizer, 0);
    assert(result.source_bytes == 2 && result.installed_bytes == 2000);
    assert(wait.wait_for(5s) == std::future_status::ready);
    assert(source->peak == 1);
}

void deselection_cancels_without_joining() {
    auto source = std::make_shared<FakeFs>();
    source->blocked = "/old.nsz";
    source->packages["/old.nsz"] = {1, 1000, 1, true, {}};
    SelectionSizer sizer(source);
    sizer.SetSelected(3, {"/old.nsz", EntryKind::File}, true);
    source->WaitEntered();
    auto deselect = std::async(std::launch::async, [&] {
        sizer.SetSelected(3, {"/old.nsz", EntryKind::File}, false);
        sizer.Cancel();
    });
    assert(deselect.wait_for(100ms) == std::future_status::ready);
    source->Release();
    sizer.WaitIdle();
    assert(sizer.Poll().empty());
}

void cancelled_queue_stays_paused_until_selection() {
    auto source = std::make_shared<FakeFs>();
    source->blocked = "/first.nsp";
    source->packages["/first.nsp"] = {1, 2, 1, true, {}};
    source->packages["/second.nsp"] = {3, 4, 1, true, {}};
    SelectionSizer sizer(source);
    sizer.SetSelected(1, {"/first.nsp", EntryKind::File}, true);
    source->WaitEntered();
    sizer.SetSelected(2, {"/second.nsp", EntryKind::File}, true);
    sizer.Cancel();
    source->Release();
    sizer.WaitIdle();
    assert(sizer.IsIdle() && source->metadata_reads == 1);
    sizer.Resume();
    std::map<std::uint64_t, SelectionSizer::Summary> completed;
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (completed.size() < 2 && std::chrono::steady_clock::now() < deadline) {
        for (auto& update : sizer.Poll()) if (update.summary.complete) completed[update.id] = update.summary;
        std::this_thread::sleep_for(1ms);
    }
    assert(completed.size() == 2);
    assert(completed[1].installed_bytes == 2 && completed[2].installed_bytes == 4);
    assert(source->metadata_reads == 3);
}

void retiring_a_view_does_not_wait_for_its_io() {
    auto source = std::make_shared<FakeFs>();
    source->blocked = "/first.nsp";
    source->packages["/first.nsp"] = {1, 2, 1, true, {}};
    auto sizer = std::make_shared<SelectionSizer>(source);
    sizer->SetSelected(99999, {"/first.nsp", EntryKind::File}, true);
    source->WaitEntered();
    auto retired = std::async(std::launch::async, [&] { SelectionSizer::Retire(std::move(sizer)); });
    assert(retired.wait_for(100ms) == std::future_status::ready);
    SelectionSizer::PollRetired();
    source->Release();
    SelectionSizer::WaitRetired();
    SelectionSizer::PollRetired();
    assert(source->active == 0);
}

void errors_are_unknown_not_free() {
    auto source = std::make_shared<FakeFs>();
    source->packages["/bad.nsz"] = {123, 0, 0, false, "invalid section table"};
    SelectionSizer sizer(source);
    sizer.SetSelected(0, {"/bad.nsz", EntryKind::File}, true);
    const auto result = Finish(sizer, 0);
    assert(result.source_bytes == 123 && result.unknown_count == 1 && !result.error.empty());
    sizer.Clear();
    source->failure = "/missing";
    sizer.SetSelected(1, {"/missing", EntryKind::Directory}, true);
    assert(Finish(sizer, 1).unknown_count == 1);
}

void overflow_never_wraps_to_a_small_estimate() {
    auto source = std::make_shared<FakeFs>();
    source->directories["/Game"] = {{"a.nsp", EntryKind::File}, {"b.nsp", EntryKind::File}, {"c.nsp", EntryKind::File}};
    source->packages["/Game/a.nsp"] = {1, std::numeric_limits<std::uint64_t>::max(), 1, true, {}};
    source->packages["/Game/b.nsp"] = {1, 20, 1, true, {}};
    source->packages["/Game/c.nsp"] = {1, 20, 1, true, {}};
    SelectionSizer sizer(source);
    sizer.SetSelected(0, {"/Game", EntryKind::Directory}, true);
    const auto result = Finish(sizer, 0);
    assert(result.installed_bytes == std::numeric_limits<std::uint64_t>::max());
    assert(result.unknown_count == 2 && !result.error.empty());
}

void unselected_cache_is_bounded() {
    auto source = std::make_shared<FakeFs>();
    source->generated = true;
    SelectionSizer sizer(source);
    for (std::size_t i = 0; i < 1025; ++i) {
        const nxloader::Root root{"/" + std::to_string(i) + ".nsp", EntryKind::File};
        sizer.SetSelected(i, root, true);
        assert(Finish(sizer, i).complete);
        sizer.SetSelected(i, root, false);
    }
    const auto count = source->metadata_reads.load();
    sizer.SetSelected(0, {"/0.nsp", EntryKind::File}, true);
    assert(Finish(sizer, 0).complete);
    assert(source->metadata_reads == count + 1);
}

void large_selection_bounded_poll_and_one_worker() {
    auto source = std::make_shared<FakeFs>();
    source->generated = true;
    SelectionSizer sizer(source);
    constexpr std::size_t count = 10000;
    for (std::size_t i = 0; i < count; ++i) {
        sizer.SetSelected(i, {"/" + std::to_string(i) + ".nsp", EntryKind::File}, true);
    }
    sizer.WaitIdle();
    std::size_t seen{};
    std::uint64_t total{};
    for (;;) {
        const auto updates = sizer.Poll();
        assert(updates.size() <= 32);
        if (updates.empty()) break;
        for (const auto& update : updates) {
            assert(update.summary.complete);
            total += update.summary.installed_bytes;
            ++seen;
        }
    }
    assert(seen == count && total == count * 100 && source->metadata_reads == count && source->peak == 1);
    sizer.Clear();
    sizer.WaitIdle();
    assert(sizer.Poll().empty());
}

void worker_start_failure_is_reported() {
    sphaira::utils::Async::fail_to_start = true;
    auto source = std::make_shared<FakeFs>();
    SelectionSizer sizer(source);
    sizer.SetSelected(0, {"/game.nsp", EntryKind::File}, true);
    const auto updates = sizer.Poll();
    assert(updates.size() == 1 && updates[0].summary.complete && updates[0].summary.unknown_count == 1);
    sizer.Clear();
    sizer.WaitIdle();
    sphaira::utils::Async::fail_to_start = false;
}

int main() {
    selected_only_and_cache();
    exclusions_reuse_catalog_and_unknowns_retry();
    cancel_and_navigation_reject_stale_results();
    deselection_cancels_without_joining();
    cancelled_queue_stays_paused_until_selection();
    retiring_a_view_does_not_wait_for_its_io();
    errors_are_unknown_not_free();
    overflow_never_wraps_to_a_small_estimate();
    unselected_cache_is_bounded();
    large_selection_bounded_poll_and_one_worker();
    worker_start_failure_is_reported();
    std::cout << "Selection size worker tests passed: cancellation, cache, unknown sizes, navigation, 10,000 selections, bounded polling.\n";
}
