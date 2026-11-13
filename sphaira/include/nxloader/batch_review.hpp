#pragma once

#include "nxloader/package_scan.hpp"

#include <map>
#include <span>

namespace nxloader {

struct ReviewGroup {
    std::string folder;
    std::vector<std::size_t> packages;
};

struct ReviewPlan {
    std::vector<ReviewGroup> groups;
    bool cancelled{};
};

namespace detail {

inline std::string_view ParentFolder(std::string_view path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string_view::npos) return ".";
    if (!slash || path[slash - 1] == ':') return path.substr(0, slash + 1);
    return path.substr(0, slash);
}

inline std::string_view RelativeTo(std::string_view path, std::string_view folder) {
    if (folder == ".") return path;
    path.remove_prefix(folder.size());
    if (path.starts_with('/')) path.remove_prefix(1);
    return path;
}

inline bool IsContentFolder(std::string_view name) {
    std::string normalized;
    for (auto c : name) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != ' ' && c != '_' && c != '-') normalized += c;
    }
    return normalized == "base" || normalized == "basegame" || normalized == "game"
        || normalized == "update" || normalized == "updates" || normalized == "patch"
        || normalized == "patches" || normalized == "dlc" || normalized == "dlcs"
        || normalized == "addon" || normalized == "addons";
}

} // namespace detail

// Folder names supply the grouping; package payloads are never opened for review.
template <class Cancelled>
ReviewPlan GroupPackages(std::span<const std::string> files, const std::vector<Root>& roots,
                        Cancelled&& cancelled) {
    std::map<std::string, std::vector<std::size_t>, std::less<>> selected;
    for (const auto& root : roots) {
        if (cancelled()) return {{}, true};
        if (root.kind == EntryKind::Directory) {
            selected.try_emplace(detail::NormalizePath(root.path));
        }
    }

    // An overlapping child selection belongs to its already-selected ancestor.
    for (auto it = selected.begin(); it != selected.end();) {
        if (cancelled()) return {{}, true};
        bool covered{};
        auto parent = detail::ParentFolder(it->first);
        while (parent != it->first) {
            if (selected.contains(parent)) {
                covered = true;
                break;
            }
            const auto next = detail::ParentFolder(parent);
            if (next == parent) break;
            parent = next;
        }
        if (covered) it = selected.erase(it);
        else ++it;
    }

    std::map<std::string, std::vector<std::size_t>, std::less<>> loose;
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (cancelled()) return {{}, true};
        auto parent = detail::ParentFolder(files[i]);
        auto found = selected.end();
        for (;;) {
            found = selected.find(parent);
            if (found != selected.end()) break;
            const auto next = detail::ParentFolder(parent);
            if (next == parent) break;
            parent = next;
        }
        if (found != selected.end()) {
            found->second.push_back(i);
        } else {
            parent = detail::ParentFolder(files[i]);
            while (detail::IsContentFolder(parent.substr(parent.find_last_of('/') + 1))) {
                const auto next = detail::ParentFolder(parent);
                if (next == parent) break;
                parent = next;
            }
            loose[std::string(parent)].push_back(i);
        }
    }

    auto grouped = std::move(loose);
    for (const auto& [folder, packages] : selected) {
        if (cancelled()) return {{}, true};
        auto& group = grouped[folder];
        group.insert(group.end(), packages.begin(), packages.end());
    }

    ReviewPlan plan;
    plan.groups.reserve(grouped.size());
    for (auto& [folder, packages] : grouped) {
        if (cancelled()) return {{}, true};
        plan.groups.push_back({folder, std::move(packages)});
    }
    return plan;
}

} // namespace nxloader
