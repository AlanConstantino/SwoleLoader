#pragma once
#include "nxloader/package_scan.hpp"
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

using u32 = std::uint32_t;
using s64 = std::int64_t;
using Result = std::uint32_t;
inline bool R_FAILED(Result result) { return result != 0; }
enum { FsDirEntryType_Dir, FsDirEntryType_File };
struct FsTimeStampRaw { bool is_valid{}; };
struct FsDirectoryEntry { char name[768]{}; int type{}; s64 file_size{}; };

namespace fs {
struct FsPath {
    char path[768]{};
    FsPath() = default;
    FsPath(const char* value) { std::strcpy(path, value); }
    FsPath(const std::string& value) : FsPath(value.c_str()) {}
    std::string toString() const { return path; }
    size_t size() const { return std::strlen(path); }
    bool ends_with(const char* suffix) const { return toString().ends_with(suffix); }
};
struct Fs {
    using Emit = std::function<bool(const nxloader::Entry&)>;
    virtual ~Fs() = default;
    virtual std::string ReadDirectory(const std::string& path, const Emit& emit) = 0;
    virtual Result FileGetSizeAndTimestamp(const FsPath& path, FsTimeStampRaw* time, s64* size) = 0;
};
}
