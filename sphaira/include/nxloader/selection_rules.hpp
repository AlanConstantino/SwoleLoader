#pragma once

#include "nxloader/package_scan.hpp"
#include <map>
#include <algorithm>

namespace nxloader {

inline std::string ParentPath(std::string_view path) {
    const auto slash = path.find_last_of('/');
    if (slash == path.npos) return {};
    if (!slash || path[slash - 1] == ':') return std::string(path.substr(0, slash + 1));
    return std::string(path.substr(0, slash));
}

inline bool WithinPath(std::string_view path, std::string_view folder) {
    return path == folder || (path.starts_with(folder) &&
        (folder.ends_with('/') || (path.size() > folder.size() && path[folder.size()] == '/')));
}

// Rules are scoped to one source. A nearer rule overrides its ancestor.
class SelectionRules {
public:
    struct Rule { EntryKind kind; bool selected; };
    struct Change { std::vector<std::string> removed; std::vector<std::string> refresh; };

    bool Selected(std::string_view path) const {
        const auto owner = Owner(path);
        return !owner.empty() && m_rules.at(owner).selected;
    }

    bool Partial(std::string_view path) const {
        auto prefix = std::string(path);
        if (!prefix.ends_with('/')) prefix += '/';
        const auto& set = Selected(path) ? m_excluded : m_selected;
        const auto it = set.lower_bound(prefix);
        return it != set.end() && it->starts_with(prefix);
    }

    std::string Owner(std::string_view path) const {
        auto current = std::string(path);
        for (;;) {
            if (m_rules.contains(current)) return current;
            const auto parent = ParentPath(current);
            if (parent == current || parent.empty()) return {};
            current = parent;
        }
    }

    Change Set(const Root& root, bool selected) {
        Change change;
        const auto path = detail::NormalizePath(root.path);
        const auto parent = ParentPath(path);
        const auto owner = parent == path ? std::string{} : Owner(parent);
        const bool inherited = !owner.empty() && m_rules.at(owner).selected;
        if (inherited) change.refresh.push_back(owner);

        const auto erase = [&](auto it) {
            if (it->second.selected) change.removed.push_back(it->first);
            m_selected.erase(it->first);
            m_excluded.erase(it->first);
            return m_rules.erase(it);
        };
        if (const auto exact = m_rules.find(path); exact != m_rules.end()) erase(exact);
        auto prefix = path;
        if (!prefix.ends_with('/')) prefix += '/';
        auto it = m_rules.lower_bound(prefix);
        while (it != m_rules.end() && it->first.starts_with(prefix)) {
            it = erase(it);
        }
        if (selected != inherited) {
            m_rules.emplace(path, Rule{root.kind, selected});
            (selected ? m_selected : m_excluded).insert(path);
            if (selected) change.refresh.push_back(path);
        }
        return change;
    }

    std::vector<std::string> Exclusions(std::string_view root) const {
        std::vector<std::string> result;
        auto prefix = std::string(root);
        if (!prefix.ends_with('/')) prefix += '/';
        for (auto it = m_excluded.lower_bound(prefix); it != m_excluded.end() && it->starts_with(prefix); ++it)
            result.push_back(*it);
        return result;
    }

    const auto& Rules() const { return m_rules; }
    std::size_t Count() const { return m_selected.size(); }

private:
    std::map<std::string, Rule, std::less<>> m_rules;
    std::set<std::string, std::less<>> m_selected, m_excluded;
};

inline bool ExcludedPath(std::string_view path, const std::vector<std::string>& excluded) {
    auto current = std::string(path);
    for (;;) {
        if (std::binary_search(excluded.begin(), excluded.end(), current)) return true;
        const auto parent = ParentPath(current);
        if (parent.empty() || parent == current) return false;
        current = parent;
    }
}

} // namespace nxloader
