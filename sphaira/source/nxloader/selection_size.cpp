#include "nxloader/selection_size.hpp"
#include "nxloader/directory_reader.hpp"
#include "nxloader/package_size_fs.hpp"
#include "nxloader/selection_rules.hpp"
#include "utils/thread.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <list>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace sphaira::nxloader_ui {

namespace {
std::mutex retirement_mutex;
std::vector<std::shared_ptr<SelectionSizer>> retired_sizers;
}

void SelectionSizer::Retire(std::shared_ptr<SelectionSizer> sizer) {
    if (!sizer) return;
    sizer->Clear();
    sizer->Cancel();
    std::lock_guard lock(retirement_mutex);
    retired_sizers.push_back(std::move(sizer));
}

void SelectionSizer::PollRetired() {
    std::lock_guard lock(retirement_mutex);
    std::erase_if(retired_sizers, [](const auto& sizer) { return sizer->IsIdle(); });
}

void SelectionSizer::WaitRetired() {
    std::vector<std::shared_ptr<SelectionSizer>> waiting;
    {
        std::lock_guard lock(retirement_mutex);
        waiting = retired_sizers;
    }
    for (const auto& sizer : waiting) sizer->WaitIdle();
}

struct SelectionSizer::Impl {
    struct Item;
    using Queue = std::list<std::shared_ptr<Item>>;
    struct Item {
        std::uint64_t id{};
        nxloader::Root root;
        std::vector<std::string> excluded;
        std::shared_ptr<const std::vector<Package>> reusable;
        std::vector<std::string> reusable_exclusions;
        Summary summary;
        bool selected{};
        bool queued{};
        bool notified{};
        bool cached{};
        Queue::iterator queue_position;
        Queue::iterator notification_position;
        Queue::iterator cache_position;
    };
    struct Task {
        std::shared_ptr<Item> item;
        std::atomic_bool cancelled{};
    };

    struct Retired {
        std::unordered_map<std::uint64_t, std::shared_ptr<Item>> items;
        Queue pending;
        Queue notifications;
        Queue cache;
    };

    static constexpr std::size_t MAX_ITEMS = 100000;
    static constexpr std::size_t MAX_UNSELECTED_CACHE = 1024;
    std::shared_ptr<fs::Fs> source;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::unordered_map<std::uint64_t, std::shared_ptr<Item>> items;
    Queue pending;
    Queue notifications;
    Queue cache;
    std::size_t cached_packages{};
    std::vector<Retired> retired;
    std::shared_ptr<Task> active;
    std::unique_ptr<utils::Async> worker;
    bool paused{};
    bool stopping{};
    bool worker_failed{};
    bool cleaning{};

    explicit Impl(std::shared_ptr<fs::Fs> source) : source{std::move(source)} {
        worker = std::make_unique<utils::Async>([this] { Run(); });
        worker_failed = !worker->IsRunning();
    }

    ~Impl() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            if (active) active->cancelled = true;
        }
        condition.notify_all();
        worker.reset();
    }

    void Notify(const std::shared_ptr<Item>& item) {
        if (!item->notified) {
            item->notification_position = notifications.insert(notifications.end(), item);
            item->notified = true;
        }
    }

    void RemoveNotification(const std::shared_ptr<Item>& item) {
        if (item->notified) {
            notifications.erase(item->notification_position);
            item->notified = false;
        }
    }

    void RemovePending(const std::shared_ptr<Item>& item) {
        if (item->queued) {
            pending.erase(item->queue_position);
            item->queued = false;
        }
    }

    void RemoveCached(const std::shared_ptr<Item>& item) {
        if (item->cached) {
            cached_packages -= item->reusable ? item->reusable->size() : item->summary.packages ? item->summary.packages->size() : 0;
            cache.erase(item->cache_position);
            item->cached = false;
        }
    }

    void Cache(const std::shared_ptr<Item>& item) {
        if (item->summary.complete && !item->selected && !item->cached) {
            item->cache_position = cache.insert(cache.end(), item);
            item->cached = true;
            cached_packages += item->reusable ? item->reusable->size() : item->summary.packages ? item->summary.packages->size() : 0;
        }
        while (cache.size() > MAX_UNSELECTED_CACHE || cached_packages > 32768) Evict();
    }

    void Evict() {
        const auto item = cache.front();
        RemoveCached(item);
        items.erase(item->id);
    }

    void Enqueue(const std::shared_ptr<Item>& item) {
        if (!item->queued) {
            item->queue_position = pending.insert(pending.end(), item);
            item->queued = true;
        }
    }

    void Publish(const std::shared_ptr<Task>& task, const Summary& summary) {
        std::lock_guard lock(mutex);
        if (!task->cancelled && task->item->selected) {
            task->item->summary = summary;
            Notify(task->item);
        }
    }

    void Measure(const std::shared_ptr<Task>& task) {
        const auto cancelled = [&] { return task->cancelled.load(std::memory_order_relaxed); };
        Summary summary;
        if (!source) {
            summary.error = "No filesystem is selected.";
            summary.unknown_count = 1;
            summary.complete = true;
            Publish(task, summary);
            return;
        }
        nxloader::ScanOptions options;
        options.max_path_length = sizeof(fs::FsPath) - 1;
        options.continue_on_read_error = true;
        nxloader::ScanResult scan;
        if (task->item->reusable) {
            scan.files.reserve(task->item->reusable->size());
            for (const auto& package : *task->item->reusable) {
                if (cancelled()) return;
                scan.files.push_back(package.path);
            }
        } else {
            scan = nxloader::Scan({task->item->root}, [&](const std::string& path, auto emit) {
                if (nxloader::ExcludedPath(path, task->item->excluded)) return std::string{};
                return ReadDirectory(source.get(), path, emit);
            }, cancelled, options);
        }
        if (scan.cancelled || cancelled()) return;
        if (!scan.error.empty()) {
            summary.error = scan.error;
            summary.unknown_count = 1;
            summary.complete = true;
            Publish(task, summary);
            return;
        }
        summary.package_count = scan.files.size();
        summary.unknown_count = scan.issues.size();
        summary.source_unknown_count = scan.issues.size();
        if (!scan.issues.empty()) summary.error = scan.issues.front().error;
        auto packages = std::make_shared<std::vector<Package>>();
        packages->reserve(scan.files.size());
        summary.package_count = 0;
        Publish(task, summary);
        auto published = std::chrono::steady_clock::now();
        const auto add = [&](std::uint64_t& total, std::uint64_t amount) {
            if (amount > std::numeric_limits<std::uint64_t>::max() - total) return false;
            total += amount;
            return true;
        };
        for (std::size_t index = 0; index < scan.files.size(); ++index) {
            const auto& path = scan.files[index];
            if (cancelled()) return;
            if (nxloader::ExcludedPath(path, task->item->excluded)) continue;
            const auto size = task->item->reusable ? (*task->item->reusable)[index].size
                : ReadPackageSize(source.get(), path, cancelled);
            if (cancelled()) return;
            packages->push_back({path, size});
            summary.source_unknown_count += !size.source_known;
            ++summary.package_count;
            if (!add(summary.source_bytes, size.source_bytes)
                || !add(summary.installed_bytes, size.installed_bytes)
                || !add(summary.content_count, size.content_count)) {
                summary.error = "Selected package sizes exceed the supported range.";
                summary.unknown_count += scan.files.size() - index;
                break;
            }
            if (!size.known) {
                ++summary.unknown_count;
                if (summary.error.empty()) summary.error = path + ": " + size.error;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now - published >= std::chrono::milliseconds(100)) {
                Publish(task, summary);
                published = now;
            }
        }
        summary.complete = true;
        summary.packages = std::move(packages);
        Publish(task, summary);
    }

    void Run() {
        for (;;) {
            std::shared_ptr<Task> task;
            std::vector<Retired> cleanup;
            {
                std::unique_lock lock(mutex);
                condition.wait(lock, [&] { return stopping || !retired.empty() || (!paused && !pending.empty()); });
                if (stopping) return;
                cleanup.swap(retired);
                cleaning = !cleanup.empty();
                if (!paused && !pending.empty()) {
                    auto item = pending.front();
                    RemovePending(item);
                    task = active = std::make_shared<Task>();
                    task->item = std::move(item);
                }
            }
            cleanup.clear();
            if (task) Measure(task);
            {
                std::lock_guard lock(mutex);
                active.reset();
                cleaning = false;
            }
            condition.notify_all();
        }
    }
};

SelectionSizer::SelectionSizer(std::shared_ptr<fs::Fs> source)
    : m_impl{std::make_unique<Impl>(std::move(source))} {}

SelectionSizer::~SelectionSizer() = default;

void SelectionSizer::SetSelected(std::uint64_t id, const nxloader::Root& root, bool selected,
                                std::vector<std::string> excluded, bool refresh) {
    std::lock_guard lock(m_impl->mutex);
    auto found = m_impl->items.find(id);
    if (!selected) {
        if (found == m_impl->items.end()) return;
        const auto item = found->second;
        item->selected = false;
        if (m_impl->active && m_impl->active->item == item) m_impl->active->cancelled = true;
        m_impl->RemovePending(item);
        m_impl->RemoveNotification(item);
        if (item->summary.complete) m_impl->Cache(item);
        else m_impl->items.erase(found);
        return;
    }

    const auto normalized = nxloader::detail::NormalizePath(root.path);
    std::shared_ptr<const std::vector<Package>> reusable;
    std::vector<std::string> reusable_exclusions;
    if (found != m_impl->items.end() &&
        (found->second->root.path != normalized || found->second->root.kind != root.kind
            || found->second->excluded != excluded || refresh || found->second->summary.unknown_count)) {
        const auto old = found->second;
        if (!refresh && old->root.path == normalized && old->root.kind == root.kind
            && old->summary.complete && !old->summary.unknown_count && old->summary.error.empty()) {
            const auto& coverage = old->reusable ? old->reusable_exclusions : old->excluded;
            if (std::ranges::all_of(coverage, [&](const auto& path) { return nxloader::ExcludedPath(path, excluded); })) {
                reusable = old->reusable ? old->reusable : old->summary.packages;
                reusable_exclusions = coverage;
            }
        }
        old->selected = false;
        if (m_impl->active && m_impl->active->item == old) m_impl->active->cancelled = true;
        m_impl->RemovePending(old);
        m_impl->RemoveNotification(old);
        m_impl->RemoveCached(old);
        m_impl->items.erase(found);
        found = m_impl->items.end();
    }
    if (found == m_impl->items.end()) {
        while (m_impl->items.size() >= Impl::MAX_ITEMS && !m_impl->cache.empty()) m_impl->Evict();
        auto item = std::make_shared<Impl::Item>();
        item->id = id;
        item->root = {normalized, root.kind};
        item->excluded = std::move(excluded);
        item->reusable = std::move(reusable);
        item->reusable_exclusions = std::move(reusable_exclusions);
        item->selected = true;
        if (m_impl->items.size() >= Impl::MAX_ITEMS) {
            item->summary = {.unknown_count = 1, .complete = true, .error = "Select no more than 100,000 entries."};
            m_impl->Notify(item);
            return;
        }
        found = m_impl->items.emplace(id, std::move(item)).first;
    }
    const auto item = found->second;
    item->selected = true;
    m_impl->RemoveCached(item);
    m_impl->paused = false;
    if (m_impl->worker_failed) {
        item->summary = {.unknown_count = 1, .complete = true, .error = "Could not start the size-calculation worker."};
    }
    if (item->summary.complete) {
        m_impl->Notify(item);
    } else if (!m_impl->active || m_impl->active->item != item || m_impl->active->cancelled) {
        item->summary = {};
        m_impl->Enqueue(item);
    }
    m_impl->condition.notify_all();
}

void SelectionSizer::Clear() {
    std::lock_guard lock(m_impl->mutex);
    if (m_impl->active) {
        m_impl->active->cancelled = true;
        m_impl->active->item->selected = false;
    }
    if (!m_impl->items.empty() || !m_impl->notifications.empty()) {
        m_impl->retired.emplace_back();
        auto& old = m_impl->retired.back();
        old.items.swap(m_impl->items);
        old.pending.swap(m_impl->pending);
        old.notifications.swap(m_impl->notifications);
        old.cache.swap(m_impl->cache);
        m_impl->cached_packages = 0;
        if (m_impl->worker_failed) m_impl->retired.clear();
    }
    m_impl->condition.notify_all();
}

void SelectionSizer::Cancel() {
    std::lock_guard lock(m_impl->mutex);
    m_impl->paused = true;
    if (m_impl->active) {
        m_impl->active->cancelled = true;
        if (m_impl->active->item->selected && !m_impl->active->item->summary.complete) m_impl->Enqueue(m_impl->active->item);
    }
    m_impl->condition.notify_all();
}

void SelectionSizer::Resume() {
    std::lock_guard lock(m_impl->mutex);
    m_impl->paused = false;
    m_impl->condition.notify_all();
}

std::vector<SelectionSizer::Update> SelectionSizer::Poll() {
    std::vector<Update> updates;
    std::lock_guard lock(m_impl->mutex);
    updates.reserve(std::min<std::size_t>(32, m_impl->notifications.size()));
    while (updates.size() < 32 && !m_impl->notifications.empty()) {
        const auto item = m_impl->notifications.front();
        m_impl->RemoveNotification(item);
        if (item->selected) updates.push_back({item->id, item->summary});
    }
    return updates;
}

bool SelectionSizer::IsIdle() const {
    std::lock_guard lock(m_impl->mutex);
    return !m_impl->active && !m_impl->cleaning && m_impl->retired.empty()
        && (m_impl->paused || m_impl->pending.empty());
}

void SelectionSizer::WaitIdle() {
    std::unique_lock lock(m_impl->mutex);
    m_impl->condition.wait(lock, [&] { return !m_impl->active && !m_impl->cleaning && m_impl->retired.empty()
        && (m_impl->paused || m_impl->pending.empty()); });
}

}
