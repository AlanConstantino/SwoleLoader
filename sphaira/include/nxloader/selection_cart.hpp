#pragma once

#include "nxloader/selection_rules.hpp"
#include "nxloader/selection_size.hpp"
#include "nxloader/capacity.hpp"
#include <optional>
#include <unordered_map>
#include <deque>

namespace sphaira::nxloader_ui {

struct SelectedRoot {
    std::string source_key;
    std::shared_ptr<fs::Fs> source;
    nxloader::Root root;
    std::vector<std::string> excluded;
    SelectionSizer::Summary size;
};

// UI-owned selection state; only immutable snapshots cross into install workers.
class SelectionCart {
public:
    ~SelectionCart();
    void Set(const std::string& source_key, const std::shared_ptr<fs::Fs>& source,
             const nxloader::Root& root, bool selected);
    bool Selected(const std::string& source_key, std::string_view path) const;
    bool Partial(const std::string& source_key, std::string_view path) const;
    std::optional<SelectionSizer::Summary> Size(const std::string& source_key, std::string_view path) const;
    void Clear();
    void Queue(const std::string& source_key, const std::shared_ptr<fs::Fs>& source, std::vector<nxloader::Root> roots);
    std::size_t Queued() const { return m_queued; }
    void Refresh(const std::string& source_key, std::string_view path);
    void Poll();
    void Resume();
    std::vector<std::shared_ptr<SelectionSizer>> Pause();
    std::vector<SelectedRoot> Snapshot() const;
    nxloader::CapacityEstimate Totals() const;
    std::size_t Count() const { return m_count + m_queued; }
    std::size_t Pending() const { return m_count - m_complete + m_queued; }
    std::uint64_t Revision() const { return m_revision; }

private:
    struct Item { std::uint64_t id; nxloader::Root root; SelectionSizer::Summary size; };
    struct Source {
        std::shared_ptr<fs::Fs> fs;
        std::shared_ptr<SelectionSizer> sizer;
        nxloader::SelectionRules rules;
        std::map<std::string, Item, std::less<>> items;
        std::unordered_map<std::uint64_t, std::string> paths;
    };
    void Adjust(const SelectionSizer::Summary& size, bool add);
    std::map<std::string, Source, std::less<>> m_sources;
    std::uint64_t m_next_id{}, m_revision{};
    std::size_t m_count{}, m_complete{};
    bool m_paused{};
    struct Bulk { std::string key; std::shared_ptr<fs::Fs> source; std::vector<nxloader::Root> roots; std::size_t cursor{}; };
    std::deque<Bulk> m_bulk;
    std::size_t m_queued{};
    unsigned __int128 m_source{}, m_installed{}, m_contents{}, m_unknown{};
};

}
