#pragma once

#include <switch.h>
#include <vector>
#include <string>
#include <span>
#include <optional>
#include "fs.hpp"
#include "utils/core.hpp"

namespace sphaira {

inline constexpr u32 NroAbiCurrentRevision = 1;

struct NroData {
    NroStart start;
    NroHeader header;
};

struct Hbini {
    u64 timestamp{}; // timestamp of last launch
    bool hidden{};
    CpuCoreMode core_mode{CpuCoreMode::Three};
};

struct MiniNacp {
    NacpLanguageEntry lang;
    char display_version[0x10];
};

struct NroEntry {
    fs::FsPath path{};
    s64 size{};
    MiniNacp nacp{};

    u64 icon_size{};
    u64 icon_offset{};
    u32 abi_revision{};

    FsTimeStampRaw timestamp{};
    Hbini hbini{};

    int image{}; // nvg image
    int x,y,w,h{}; // image
    bool is_nacp_valid{};
    std::optional<bool> has_star{std::nullopt};

    auto GetName() const -> const char* {
        return nacp.lang.name;
    }

    auto GetAuthor() const -> const char* {
        return nacp.lang.author;
    }

    auto GetDisplayVersion() const -> const char* {
        return nacp.display_version;
    }

    auto IsAbiOutdated() const -> bool {
        return abi_revision < NroAbiCurrentRevision;
    }
};

auto nro_parse(const fs::FsPath& path, NroEntry& entry) -> Result;

auto nro_get_nacp(const fs::FsPath& path, NacpStruct& nacp) -> Result;

// path is pre-appended to args, such that argv[0] == path
auto nro_launch(std::string path, std::string args = {}) -> Result;

// if the arg contains a space, it will wrap it in quotes
auto nro_add_arg(std::string arg) -> std::string;

} // namespace sphaira
