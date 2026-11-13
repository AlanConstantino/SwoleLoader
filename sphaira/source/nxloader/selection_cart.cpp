#include "nxloader/selection_cart.hpp"
#include <limits>

namespace sphaira::nxloader_ui {

SelectionCart::~SelectionCart() {
    for (auto& [key, source] : m_sources) SelectionSizer::Retire(std::move(source.sizer));
}

void SelectionCart::Adjust(const SelectionSizer::Summary& size, bool add) {
    const auto adjust = [add](auto& value, auto amount) { if (add) value += amount; else value -= amount; };
    adjust(m_source, size.source_bytes);
    adjust(m_installed, size.installed_bytes);
    adjust(m_contents, size.content_count);
    adjust(m_unknown, size.unknown_count);
    adjust(m_complete, size.complete ? 1 : 0);
}

void SelectionCart::Set(const std::string& key, const std::shared_ptr<fs::Fs>& fs,
                        const nxloader::Root& root, bool selected) {
    auto& source = m_sources[key];
    if (!source.sizer) {
        source.fs = fs;
        source.sizer = std::make_shared<SelectionSizer>(fs);
    }
    const auto removed_size = selected ? std::optional<SelectionSizer::Summary>{} : Size(key, root.path);
    const auto changes = source.rules.Set(root, selected);
    for (const auto& path : changes.removed) {
        const auto it = source.items.find(path);
        if (it == source.items.end()) continue;
        Adjust(it->second.size, false);
        source.sizer->SetSelected(it->second.id, it->second.root, false);
        source.paths.erase(it->second.id);
        source.items.erase(it);
        --m_count;
    }
    for (const auto& path : changes.refresh) {
        auto [it, inserted] = source.items.try_emplace(path, Item{++m_next_id, {path, source.rules.Rules().at(path).kind}, {}});
        auto& item = it->second;
        if (inserted) { ++m_count; source.paths.emplace(item.id, path); }
        else {
            auto remaining = item.size;
            Adjust(item.size, false);
            if (selected) {
                remaining.complete = false;
                remaining.packages.reset();
            } else if (removed_size && remaining.complete && removed_size->complete
                && remaining.source_bytes >= removed_size->source_bytes
                && remaining.installed_bytes >= removed_size->installed_bytes
                && remaining.content_count >= removed_size->content_count
                && remaining.package_count >= removed_size->package_count
                && remaining.unknown_count >= removed_size->unknown_count
                && remaining.source_unknown_count >= removed_size->source_unknown_count) {
                remaining.source_bytes -= removed_size->source_bytes;
                remaining.installed_bytes -= removed_size->installed_bytes;
                remaining.content_count -= removed_size->content_count;
                remaining.package_count -= removed_size->package_count;
                remaining.unknown_count -= removed_size->unknown_count;
                remaining.source_unknown_count -= removed_size->source_unknown_count;
                remaining.complete = false;
                remaining.packages.reset();
            } else remaining = {};
            item.size = std::move(remaining);
            Adjust(item.size, true);
        }
        source.sizer->SetSelected(item.id, item.root, true, source.rules.Exclusions(path));
    }
    if (m_paused) source.sizer->Cancel();
    ++m_revision;
}

void SelectionCart::Refresh(const std::string& key, std::string_view path) {
    auto source = m_sources.find(key);
    if (source == m_sources.end()) return;
    auto& state = source->second;
    const auto owner = state.rules.Owner(path);
    auto item = state.items.find(owner);
    if (item == state.items.end()) return;
    Adjust(item->second.size, false);
    item->second.size = {};
    state.sizer->SetSelected(item->second.id, item->second.root, true, state.rules.Exclusions(owner), true);
    if (m_paused) state.sizer->Cancel();
}

bool SelectionCart::Selected(const std::string& key, std::string_view path) const {
    const auto source = m_sources.find(key);
    return source != m_sources.end() && source->second.rules.Selected(path);
}

bool SelectionCart::Partial(const std::string& key, std::string_view path) const {
    const auto source = m_sources.find(key);
    return source != m_sources.end() && source->second.rules.Partial(path);
}

std::optional<SelectionSizer::Summary> SelectionCart::Size(const std::string& key, std::string_view path) const {
    const auto source = m_sources.find(key);
    if (source == m_sources.end()) return {};
    const auto& state = source->second;
    if (!state.rules.Selected(path)) return {};
    const auto owner = state.rules.Owner(path);
    const auto item = state.items.find(owner);
    if (item == state.items.end()) return {};
    if (owner == path) return item->second.size;
    if (const auto& packages = item->second.size.packages) {
        const auto it = std::lower_bound(packages->begin(), packages->end(), path,
            [](const auto& package, auto name) { return package.path < name; });
        if (it != packages->end() && it->path == path) {
            const auto& size = it->size;
            return SelectionSizer::Summary{size.source_bytes, size.installed_bytes, size.content_count,
                1, size.known ? 0U : 1U, true, size.error, {}, size.source_known ? 0U : 1U};
        }
    }
    return {};
}

void SelectionCart::Clear() {
    m_bulk.clear();
    m_queued = 0;
    for (auto& [key, source] : m_sources) {
        source.sizer->Clear();
        source.rules = {};
        source.items.clear();
        source.paths.clear();
    }
    m_count = m_complete = 0;
    m_source = m_installed = m_contents = m_unknown = 0;
    ++m_revision;
}

void SelectionCart::Queue(const std::string& key, const std::shared_ptr<fs::Fs>& source, std::vector<nxloader::Root> roots) {
    m_queued += roots.size();
    m_bulk.push_back({key, source, std::move(roots), 0});
}

void SelectionCart::Poll() {
    for (unsigned i = 0; i < 32 && !m_bulk.empty();) {
        auto& bulk = m_bulk.front();
        if (bulk.cursor == bulk.roots.size()) { m_bulk.pop_front(); continue; }
        const auto& root = bulk.roots[bulk.cursor++];
        --m_queued;
        ++i;
        if (!Selected(bulk.key, root.path) || Partial(bulk.key, root.path)) Set(bulk.key, bulk.source, root, true);
    }
    for (auto& [key, source] : m_sources) {
        for (auto& update : source.sizer->Poll()) {
            const auto path = source.paths.find(update.id);
            if (path == source.paths.end()) continue;
            auto& item = source.items.at(path->second);
            Adjust(item.size, false);
            item.size = std::move(update.summary);
            Adjust(item.size, true);
        }
    }
}

void SelectionCart::Resume() { if (m_paused) { m_paused = false; for (auto& [key, source] : m_sources) source.sizer->Resume(); } }

std::vector<std::shared_ptr<SelectionSizer>> SelectionCart::Pause() {
    m_paused = true;
    std::vector<std::shared_ptr<SelectionSizer>> result;
    for (auto& [key, source] : m_sources) { source.sizer->Cancel(); result.push_back(source.sizer); }
    return result;
}

std::vector<SelectedRoot> SelectionCart::Snapshot() const {
    std::vector<SelectedRoot> result;
    result.reserve(m_count);
    for (const auto& [key, source] : m_sources)
        for (const auto& [path, item] : source.items)
            result.push_back({key, source.fs, item.root, source.rules.Exclusions(path), item.size});
    return result;
}

nxloader::CapacityEstimate SelectionCart::Totals() const {
    nxloader::CapacityEstimate total;
    const auto copy = [&](auto& out, auto value) {
        if (value > std::numeric_limits<std::uint64_t>::max()) total.overflow = true;
        else out = static_cast<std::uint64_t>(value);
    };
    copy(total.source_bytes, m_source);
    copy(total.installed_bytes, m_installed);
    copy(total.content_count, m_contents);
    copy(total.unknown_count, m_unknown + Pending());
    return total;
}

}
