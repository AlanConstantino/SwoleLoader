#include "nxloader/browser.hpp"
#include "nxloader/directory_reader.hpp"
#include "utils/thread.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <numeric>
#include <optional>
#include <string_view>

namespace sphaira::nxloader_ui {
namespace {

using ui::menu::filebrowser::FileEntry;
using ui::menu::filebrowser::SortType_Size;
using ui::menu::filebrowser::OrderType_Descending;

constexpr size_t MAX_ENTRIES = 100000;

bool MatchesFilter(const FileEntry& entry, const std::vector<std::string>& filter) {
    if (entry.IsHidden()) {
        return false;
    }
    if (entry.IsDir() || filter.empty()) {
        return true;
    }
    const auto dot = std::strrchr(entry.name, '.');
    return dot && std::ranges::any_of(filter, [dot](const auto& extension) {
        return !strcasecmp(dot + 1, extension.c_str());
    });
}

}

struct BrowserLoader::Impl {
    struct Task {
        Request request;
        std::vector<Request> retired_requests;
        std::unique_ptr<Snapshot> snapshot;
        std::atomic_bool cancelled{};
        std::atomic_bool finished{};
        std::atomic_size_t count{};
    };

    std::optional<Request> pending;
    std::vector<Request> retired_requests;
    std::shared_ptr<Task> active;
    std::unique_ptr<utils::Async> worker;

    static void Load(Task& task) {
        task.retired_requests.clear();
        auto result = std::make_unique<Snapshot>();
        auto& request = task.request;
        result->entries = std::move(request.recycled_entries);
        if (!request.reuse_entries) {
            result->entries.clear();
        }
        task.count.store(result->entries.size(), std::memory_order_relaxed);

        const auto cancelled = [&] { return task.cancelled.load(std::memory_order_relaxed); };
        const auto fail = [&](std::string error) {
            result->error = std::move(error);
            result->entries.clear();
            result->visible_indices.clear();
            result->all_indices.clear();
            result->selected_count = 0;
        };

        if (!request.source) {
            fail("No filesystem is selected.");
        } else if (result->entries.size() > MAX_ENTRIES) {
            fail("Folder exceeds 100,000 entries; split it into smaller folders.");
        } else if (!request.reuse_entries && !cancelled()) {
            const auto error = ReadDirectory(request.source.get(), request.path.toString(), [&](const nxloader::Entry& entry) {
                if (cancelled()) {
                    return false;
                }
                if (entry.kind == nxloader::EntryKind::Other || entry.name == "." || entry.name == "..") {
                    return true;
                }
                if (entry.name.empty() || entry.name.find('/') != entry.name.npos || entry.name.find('\0') != entry.name.npos ||
                    entry.name.size() >= sizeof(FsDirectoryEntry::name) ||
                    request.path.size() + (request.path.ends_with("/") ? 0 : 1) + entry.name.size() >= sizeof(fs::FsPath)) {
                    result->error = "A filename or path exceeds the supported length.";
                    return false;
                }
                if (result->entries.size() >= MAX_ENTRIES) {
                    result->error = "Folder exceeds 100,000 entries; split it into smaller folders.";
                    return false;
                }
                FileEntry file{};
                std::memcpy(file.name, entry.name.c_str(), entry.name.size() + 1);
                file.type = entry.kind == nxloader::EntryKind::Directory ? FsDirEntryType_Dir : FsDirEntryType_File;
                file.file_size = -1;
                result->entries.push_back(std::move(file));
                task.count.store(result->entries.size(), std::memory_order_relaxed);
                return true;
            });
            if (!error.empty()) {
                result->error = error;
            }
        }

        if (!result->error.empty()) {
            fail(std::move(result->error));
        } else if (!cancelled()) {
            for (auto& entry : result->entries) {
                if (cancelled()) {
                    break;
                }
                result->selected_count += entry.selected;
                if (request.sort == SortType_Size && entry.IsFile() && entry.file_size < 0) {
                    auto path = request.path.toString();
                    if (!path.ends_with('/')) {
                        path += '/';
                    }
                    path += entry.name;
                    if (path.size() >= sizeof(fs::FsPath)) {
                        result->error = "A filename or path exceeds the supported length.";
                        break;
                    }
                    const auto rc = request.source->FileGetSizeAndTimestamp(path, &entry.time_stamp, &entry.file_size);
                    if (R_FAILED(rc)) {
                        result->error = "Could not read file metadata: " + path;
                        break;
                    }
                    entry.done_stat = true;
                }
            }

            if (!result->error.empty()) {
                fail(std::move(result->error));
            } else if (!cancelled()) {
                result->all_indices.resize(result->entries.size());
                std::iota(result->all_indices.begin(), result->all_indices.end(), u32{});
                std::sort(result->all_indices.begin(), result->all_indices.end(), [&](u32 left, u32 right) {
                    const auto& lhs = result->entries[left];
                    const auto& rhs = result->entries[right];
                    if (request.hidden_last && lhs.IsHidden() != rhs.IsHidden()) {
                        return !lhs.IsHidden();
                    }
                    if (request.folders_first && lhs.IsDir() != rhs.IsDir()) {
                        return lhs.IsDir();
                    }
                    if (request.sort == SortType_Size && lhs.file_size != rhs.file_size) {
                        return request.order == OrderType_Descending ? lhs.file_size > rhs.file_size : lhs.file_size < rhs.file_size;
                    }
                    const auto comparison = strncasecmp(lhs.name, rhs.name, sizeof(lhs.name));
                    return request.sort == SortType_Size || request.order == OrderType_Descending ? comparison < 0 : comparison > 0;
                });
                result->visible_indices.reserve(result->all_indices.size());
                for (const auto index : result->all_indices) {
                    if (cancelled()) {
                        break;
                    }
                    if (MatchesFilter(result->entries[index], request.filter)) {
                        result->visible_indices.push_back(index);
                    }
                }
            }
        }

        if (cancelled()) {
            result.reset();
        }
        task.count.store(result ? result->entries.size() : 0, std::memory_order_relaxed);
        task.snapshot = std::move(result);
        request.source.reset();
        task.finished.store(true, std::memory_order_release);
    }

    void StartPending() {
        if (active || !pending) {
            return;
        }
        active = std::make_shared<Task>();
        active->request = std::move(*pending);
        pending.reset();
        active->retired_requests = std::move(retired_requests);
        worker = std::make_unique<utils::Async>([task = active] { Load(*task); });
        if (!worker->IsRunning()) {
            active->snapshot = std::make_unique<Snapshot>();
            active->snapshot->error = "Could not start the directory-loading worker.";
            active->finished.store(true, std::memory_order_release);
        }
    }

    void RetirePending() {
        if (pending) {
            retired_requests.push_back(std::move(*pending));
            pending.reset();
        }
    }
};

BrowserLoader::BrowserLoader() : m_impl{std::make_unique<Impl>()} {}

BrowserLoader::~BrowserLoader() {
    Cancel();
    m_impl->worker.reset();
}

void BrowserLoader::Submit(Request request) {
    Cancel();
    m_impl->pending = std::move(request);
    m_impl->StartPending();
}

void BrowserLoader::Cancel() {
    m_impl->RetirePending();
    if (m_impl->active) {
        m_impl->active->cancelled.store(true, std::memory_order_relaxed);
    }
}

std::unique_ptr<BrowserLoader::Snapshot> BrowserLoader::Poll() {
    std::unique_ptr<Snapshot> result;
    if (m_impl->active && m_impl->active->finished.load(std::memory_order_acquire)) {
        m_impl->worker.reset();
        if (!m_impl->active->cancelled.load(std::memory_order_relaxed)) {
            result = std::move(m_impl->active->snapshot);
        }
        m_impl->active.reset();
    }
    m_impl->StartPending();
    return result;
}

bool BrowserLoader::IsLoading() const {
    return m_impl->pending.has_value() || (m_impl->active && !m_impl->active->cancelled.load(std::memory_order_relaxed));
}

size_t BrowserLoader::Count() const {
    if (m_impl->pending || !m_impl->active || m_impl->active->cancelled.load(std::memory_order_relaxed)) {
        return 0;
    }
    return m_impl->active->count.load(std::memory_order_relaxed);
}

}
