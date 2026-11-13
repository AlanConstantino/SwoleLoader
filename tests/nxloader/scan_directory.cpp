#include "nxloader/package_scan.hpp"

#include <filesystem>
#include <iostream>
#include <system_error>

namespace fs = std::filesystem;

nxloader::EntryKind Kind(fs::file_status status) {
    if (fs::is_regular_file(status)) {
        return nxloader::EntryKind::File;
    }
    if (fs::is_directory(status)) {
        return nxloader::EntryKind::Directory;
    }
    return nxloader::EntryKind::Other;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: scan_directory ROOT [ROOT ...]\n";
        return 2;
    }
    std::vector<nxloader::Root> roots;
    for (int i = 1; i < argc; ++i) {
        std::error_code error;
        const auto path = fs::absolute(argv[i], error).lexically_normal();
        if (error) {
            std::cerr << error.message() << '\n';
            return 1;
        }
        const auto status = fs::symlink_status(path, error);
        if (error) {
            std::cerr << path << ": " << error.message() << '\n';
            return 1;
        }
        roots.push_back({path.string(), Kind(status)});
    }

    const auto read_directory = [](const std::string& path, const auto& emit) -> std::string {
        std::error_code error;
        auto entry = fs::directory_iterator(path, error);
        const auto end = fs::directory_iterator();
        while (!error && entry != end) {
            const auto status = entry->symlink_status(error);
            if (error || !emit({entry->path().filename().string(), Kind(status)})) {
                break;
            }
            entry.increment(error);
        }
        return error ? error.message() : std::string{};
    };
    const auto result = nxloader::Scan(roots, read_directory, [] { return false; });
    if (!result.error.empty()) {
        std::cerr << result.error << '\n';
        return 1;
    }
    for (const auto& path : result.files) {
        std::cout << path << '\n';
    }
    std::cerr << result.files.size() << " packages in " << result.directories << " directories.\n";
}
