#pragma once

#include "fs.hpp"
#include "defines.hpp"
#include "nxloader/package_scan.hpp"
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace sphaira::nxloader_ui {

inline std::string DirectoryReadError(const std::string& path, Result rc) {
    char code[32];
    std::snprintf(code, sizeof(code), " (0x%08X)", rc);
    return path + code;
}

template<class Emit>
std::string ReadDirectory(fs::Fs* source, const std::string& path, Emit emit) {
    if (source->IsNative()) {
        fs::Dir directory;
        auto rc = source->OpenDirectory(path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles | FsDirOpenMode_NoFileSize, &directory);
        if (R_FAILED(rc)) {
            return DirectoryReadError(path, rc);
        }
        std::array<FsDirectoryEntry, 8> entries{};
        for (;;) {
            s64 count{};
            rc = directory.Read(&count, entries.size(), entries.data());
            if (R_FAILED(rc)) {
                return DirectoryReadError(path, rc);
            }
            if (!count) {
                return {};
            }
            for (s64 i = 0; i < count; ++i) {
                const auto kind = entries[i].type == FsDirEntryType_Dir ? nxloader::EntryKind::Directory : nxloader::EntryKind::File;
                if (!emit(nxloader::Entry{entries[i].name, kind})) {
                    return {};
                }
            }
        }
    }

    auto directory = opendir(path.c_str());
    if (!directory) {
        return path + ": " + std::strerror(errno);
    }
    ON_SCOPE_EXIT(closedir(directory));
    for (;;) {
        errno = 0;
        auto entry = readdir(directory);
        if (!entry) {
            return errno ? path + ": " + std::strerror(errno) : std::string{};
        }
        auto kind = nxloader::EntryKind::Other;
        if (entry->d_type == DT_DIR) {
            kind = nxloader::EntryKind::Directory;
        } else if (entry->d_type == DT_REG) {
            kind = nxloader::EntryKind::File;
        } else if (entry->d_type == DT_UNKNOWN) {
            struct stat info{};
            const auto full_path = path + "/" + entry->d_name;
            if (lstat(full_path.c_str(), &info)) {
                return full_path + ": " + std::strerror(errno);
            }
            if (S_ISDIR(info.st_mode)) {
                kind = nxloader::EntryKind::Directory;
            } else if (S_ISREG(info.st_mode)) {
                kind = nxloader::EntryKind::File;
            }
        }
        if (!emit(nxloader::Entry{entry->d_name, kind})) {
            return {};
        }
    }
}

}
