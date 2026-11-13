#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nxloader {

enum class EntryKind { File, Directory, Other };

struct Entry {
    std::string name;
    EntryKind kind;
};

struct Root {
    std::string path;
    EntryKind kind;
};

struct ScanOptions {
    std::size_t max_files{32768};
    std::size_t max_directories{16384};
    std::size_t max_path_length{767};
    bool continue_on_read_error{};
};

struct ScanResult {
    std::vector<std::string> files;
    std::string error;
    bool cancelled{};
    std::size_t directories{};
    struct Issue { std::string path; std::string error; };
    std::vector<Issue> issues;
};

namespace detail {

inline std::string NormalizePath(std::string_view path) {
    std::string prefix;
    std::size_t position{};
    const auto colon = path.find(':');
    const auto slash = path.find('/');
    if (colon != std::string_view::npos &&
        (slash == std::string_view::npos || colon < slash)) {
        prefix = std::string(path.substr(0, colon + 1)) + '/';
        position = colon + 1;
    } else if (path.starts_with('/')) {
        prefix = "/";
    }

    std::vector<std::string_view> parts;
    while (position < path.size()) {
        const auto end = path.find('/', position);
        const auto part = path.substr(position, end - position);
        if (!part.empty() && part != ".") {
            if (part == ".." && !parts.empty() && parts.back() != "..") {
                parts.pop_back();
            } else if (part != ".." || prefix.empty()) {
                parts.push_back(part);
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        position = end + 1;
    }

    std::string normalized = std::move(prefix);
    for (const auto part : parts) {
        if (!normalized.empty() && normalized.back() != '/') {
            normalized += '/';
        }
        normalized += part;
    }
    return normalized.empty() ? "." : normalized;
}

inline bool IsPackage(std::string_view path) {
    const auto name = path.substr(path.find_last_of('/') + 1);
    if (name.starts_with("._")) {
        return false;
    }
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos || name.size() - dot != 4) {
        return false;
    }
    char extension[3];
    for (std::size_t i = 0; i < 3; ++i) {
        const char letter = name[dot + i + 1];
        extension[i] = letter >= 'A' && letter <= 'Z' ? letter + ('a' - 'A') : letter;
    }
    const std::string_view ext(extension, 3);
    return ext == "nsp" || ext == "nsz" || ext == "xci" || ext == "xcz" || ext == "msp";
}

} // namespace detail

// read_directory(path, emit) returns an empty string on success. It must stop
// enumerating when emit(const Entry&) returns false. Other entries are ignored.
template <class ReadDirectory, class Cancelled>
ScanResult Scan(const std::vector<Root>& roots, ReadDirectory&& read_directory,
                Cancelled&& cancelled, const ScanOptions& options = {}) {
    ScanResult result;
    std::set<std::string> files;
    std::unordered_set<std::string> directories;
    std::vector<std::string> pending;

    const auto should_stop = [&]() {
        if (cancelled()) {
            result.cancelled = true;
        }
        return result.cancelled || !result.error.empty();
    };

    const auto add = [&](std::string path, EntryKind kind) {
        if (kind == EntryKind::Other || (kind == EntryKind::File && !detail::IsPackage(path))) {
            return true;
        }
        if (path.size() > options.max_path_length) {
            result.error = "Path exceeds the supported length: " + path;
            return false;
        }
        if (kind == EntryKind::File) {
            if (files.contains(path)) {
                return true;
            }
            if (files.size() >= options.max_files) {
                result.error = "Package limit reached; select fewer folders.";
                return false;
            }
            files.insert(std::move(path));
        } else {
            if (directories.contains(path)) {
                return true;
            }
            if (directories.size() >= options.max_directories) {
                result.error = "Directory limit reached; select fewer folders.";
                return false;
            }
            directories.insert(path);
            pending.push_back(std::move(path));
        }
        return true;
    };

    for (const auto& root : roots) {
        if (should_stop()) {
            return result;
        }
        if (root.kind == EntryKind::Other) {
            continue;
        }
        if (root.path.empty() || root.path.find('\0') != std::string::npos) {
            result.error = "Invalid selected path.";
            return result;
        }
        if (!add(detail::NormalizePath(root.path), root.kind)) {
            return result;
        }
    }

    while (!pending.empty()) {
        if (should_stop()) {
            return result;
        }
        auto path = std::move(pending.back());
        pending.pop_back();
        ++result.directories;

        const auto emit = [&](const Entry& entry) {
            if (should_stop()) {
                return false;
            }
            if (entry.kind == EntryKind::Other || entry.name == "." || entry.name == ".." ||
                (entry.kind == EntryKind::Directory && entry.name.starts_with('.'))) {
                return true;
            }
            if (entry.name.empty() || entry.name.find('/') != std::string::npos ||
                entry.name.find('\0') != std::string::npos) {
                result.error = "Invalid directory entry in " + path;
                return false;
            }
            std::string child = path == "." ? std::string{} : path;
            if (!child.empty() && child.back() != '/') {
                child += '/';
            }
            child += entry.name;
            return add(std::move(child), entry.kind);
        };

        const auto error = read_directory(path, emit);
        if (should_stop()) {
            return result;
        }
        if (!error.empty()) {
            if (options.continue_on_read_error) {
                result.issues.push_back({path, error});
                continue;
            }
            result.error = "Could not scan " + path + ": " + error;
            return result;
        }
    }

    result.files.reserve(files.size());
    while (!files.empty()) {
        auto file = files.extract(files.begin());
        result.files.push_back(std::move(file.value()));
    }
    return result;
}

} // namespace nxloader
