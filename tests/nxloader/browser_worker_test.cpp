#include "nxloader/browser.hpp"
#include "utils/thread.hpp"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>

using sphaira::nxloader_ui::BrowserLoader;
using namespace sphaira::ui::menu::filebrowser;
using namespace std::chrono_literals;

struct FakeFs final : fs::Fs {
    std::map<std::string, std::vector<nxloader::Entry>> directories;
    std::map<std::string, s64> sizes;
    std::atomic_size_t reads{}, stats{};
    std::atomic_size_t active{}, peak{};
    std::mutex mutex;
    std::condition_variable condition;
    bool entered{}, released{};
    std::string blocked_path, error_path;
    size_t generated{};

    std::string ReadDirectory(const std::string& path, const Emit& emit) override {
        ++reads;
        const auto running = ++active;
        peak.store(std::max(peak.load(), running));
        if (path == blocked_path) {
            std::unique_lock lock(mutex);
            entered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return released; });
        }
        if (generated) {
            for (size_t i = 0; i < generated; ++i) {
                if (!emit({"Game " + std::to_string(generated - i) + ".nsp", nxloader::EntryKind::File})) break;
            }
        } else {
            for (const auto& entry : directories.at(path)) {
                if (!emit(entry)) break;
            }
        }
        --active;
        return path == error_path ? "drive removed" : std::string{};
    }

    Result FileGetSizeAndTimestamp(const fs::FsPath& path, FsTimeStampRaw* time, s64* size) override {
        ++stats;
        const auto found = sizes.find(path.toString());
        if (found == sizes.end()) return 1;
        *size = found->second;
        time->is_valid = true;
        return 0;
    }

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

BrowserLoader::Request Request(const std::shared_ptr<FakeFs>& source, const char* path) {
    BrowserLoader::Request request;
    request.source = source;
    request.path = path;
    return request;
}

std::unique_ptr<BrowserLoader::Snapshot> Finish(BrowserLoader& loader) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto result = loader.Poll()) return result;
        std::this_thread::sleep_for(1ms);
    }
    assert(false && "directory worker did not finish");
    return {};
}

std::vector<std::string> Names(const BrowserLoader::Snapshot& snapshot, bool hidden = false) {
    std::vector<std::string> names;
    for (auto index : hidden ? snapshot.all_indices : snapshot.visible_indices) names.emplace_back(snapshot.entries[index].name);
    return names;
}

void FilterSortAndReuse() {
    const auto source = std::make_shared<FakeFs>();
    source->directories["/"] = {{"z.NSZ", nxloader::EntryKind::File}, {".hidden.nsp", nxloader::EntryKind::File},
        {"b.nsp", nxloader::EntryKind::File}, {"games", nxloader::EntryKind::Directory},
        {"readme.txt", nxloader::EntryKind::File}, {"link", nxloader::EntryKind::Other}};
    BrowserLoader loader;
    auto request = Request(source, "/");
    request.filter = {"nsp", "nsz"};
    request.hidden_last = true;
    loader.Submit(std::move(request));
    auto result = Finish(loader);
    assert(result->error.empty());
    assert((Names(*result) == std::vector<std::string>{"games", "b.nsp", "z.NSZ"}));
    assert((Names(*result, true) == std::vector<std::string>{"games", "b.nsp", "readme.txt", "z.NSZ", ".hidden.nsp"}));
    assert(source->reads == 1 && source->stats == 0);
    assert(!loader.IsLoading());

    result->entries[result->visible_indices[1]].selected = true;
    request = Request(source, "/");
    request.reuse_entries = true;
    request.recycled_entries = std::move(result->entries);
    request.order = OrderType_Ascending;
    loader.Submit(std::move(request));
    result = Finish(loader);
    assert((Names(*result) == std::vector<std::string>{"games", "z.NSZ", "readme.txt", "b.nsp"}));
    assert(result->selected_count == 1);
    assert(result->entries[result->visible_indices.back()].selected);
    assert(source->reads == 1 && source->stats == 0);
}

void MetadataOnlyForSizeSort() {
    const auto source = std::make_shared<FakeFs>();
    source->directories["/"] = {{"a.nsp", nxloader::EntryKind::File}, {"b.nsp", nxloader::EntryKind::File}};
    source->sizes = {{"/a.nsp", 20}, {"/b.nsp", 100}};
    BrowserLoader loader;
    auto request = Request(source, "/");
    request.sort = SortType_Size;
    loader.Submit(std::move(request));
    auto result = Finish(loader);
    assert(result->error.empty() && source->stats == 2);
    assert((Names(*result) == std::vector<std::string>{"b.nsp", "a.nsp"}));
    request = Request(source, "/");
    request.sort = SortType_Size;
    request.order = OrderType_Ascending;
    request.reuse_entries = true;
    request.recycled_entries = std::move(result->entries);
    loader.Submit(std::move(request));
    result = Finish(loader);
    assert((Names(*result) == std::vector<std::string>{"a.nsp", "b.nsp"}));
    assert(source->stats == 2 && source->reads == 1);
}

void SupersededWorkAndCancel() {
    const auto source = std::make_shared<FakeFs>();
    source->directories = {{"/old", {{"old.nsp", nxloader::EntryKind::File}}},
        {"/middle", {{"middle.nsp", nxloader::EntryKind::File}}}, {"/new", {{"new.nsp", nxloader::EntryKind::File}}}};
    source->blocked_path = "/old";
    BrowserLoader loader;
    loader.Submit(Request(source, "/old"));
    source->WaitEntered();
    auto submit = std::async(std::launch::async, [&] {
        loader.Submit(Request(source, "/middle"));
        loader.Submit(Request(source, "/new"));
    });
    const auto returned_before_release = submit.wait_for(1s) == std::future_status::ready;
    if (!returned_before_release) source->Release();
    submit.get();
    assert(returned_before_release && "Submit joined blocked directory I/O");
    assert(loader.IsLoading() && !loader.Poll());
    source->Release();
    const auto result = Finish(loader);
    assert((Names(*result) == std::vector<std::string>{"new.nsp"}));
    assert(source->reads == 2 && source->peak == 1);

    source->entered = false;
    source->released = false;
    loader.Submit(Request(source, "/old"));
    source->WaitEntered();
    auto cancel = std::async(std::launch::async, [&] { loader.Cancel(); });
    const auto cancelled_before_release = cancel.wait_for(1s) == std::future_status::ready;
    if (!cancelled_before_release) source->Release();
    cancel.get();
    assert(cancelled_before_release && !loader.IsLoading());
    source->Release();
    loader.Submit(Request(source, "/new"));
    assert((Names(*Finish(loader)) == std::vector<std::string>{"new.nsp"}));
}

void FailedOrOversizedFoldersDoNotPublishPartialData() {
    const auto source = std::make_shared<FakeFs>();
    source->directories["/error"] = {{"partial.nsp", nxloader::EntryKind::File}};
    source->error_path = "/error";
    BrowserLoader loader;
    loader.Submit(Request(source, "/error"));
    auto result = Finish(loader);
    assert(result->error == "drive removed" && result->entries.empty() && result->all_indices.empty());

    source->generated = 100001;
    loader.Submit(Request(source, "/large"));
    result = Finish(loader);
    assert(!result->error.empty() && result->entries.empty() && result->visible_indices.empty());
    source->generated = 100000;
    loader.Submit(Request(source, "/large"));
    result = Finish(loader);
    assert(result->error.empty() && result->entries.size() == 100000 && result->visible_indices.size() == 100000);
    assert(source->stats == 0);

    source->generated = 0;
    source->directories["/long"] = {{std::string(768, 'x'), nxloader::EntryKind::File}};
    loader.Submit(Request(source, "/long"));
    result = Finish(loader);
    assert(!result->error.empty() && result->entries.empty());
}

void FailedThreadStartIsReported() {
    sphaira::utils::Async::fail_to_start = true;
    BrowserLoader loader;
    loader.Submit(Request(std::make_shared<FakeFs>(), "/"));
    const auto result = Finish(loader);
    assert(!result->error.empty() && !loader.IsLoading());
    sphaira::utils::Async::fail_to_start = false;
}

int main() {
    FilterSortAndReuse();
    MetadataOnlyForSizeSort();
    SupersededWorkAndCancel();
    FailedOrOversizedFoldersDoNotPublishPartialData();
    FailedThreadStartIsReported();
    std::cout << "Browser worker tests passed: latest request, cancellation, reuse, errors, 100,000 entries.\n";
}
