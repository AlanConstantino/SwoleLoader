#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nxloader {

// One line per placeholder an install creates, so the partly written data of
// an install cut off by power loss can be found and removed on the next
// launch. A line is the storage id and the placeholder id in hex.
struct PlaceholderEntry {
    std::uint8_t storage_id{};
    std::array<std::uint8_t, 16> id{};
};

inline std::string FormatPlaceholderEntry(const PlaceholderEntry& entry) {
    constexpr char hex[] = "0123456789abcdef";
    std::string line = std::to_string(entry.storage_id) + ' ';
    for (const auto byte : entry.id) {
        line += hex[byte >> 4];
        line += hex[byte & 0xF];
    }
    return line + '\n';
}

inline std::optional<PlaceholderEntry> ParsePlaceholderEntry(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.remove_suffix(1);
    const auto space = line.find(' ');
    if (space == 0 || space > 3 || line.size() != space + 1 + 32) return std::nullopt;

    unsigned storage{};
    for (const auto c : line.substr(0, space)) {
        if (c < '0' || c > '9') return std::nullopt;
        storage = storage * 10 + (c - '0');
    }
    if (storage > 0xFF) return std::nullopt;

    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    PlaceholderEntry entry{static_cast<std::uint8_t>(storage), {}};
    const auto digits = line.substr(space + 1);
    for (std::size_t i = 0; i < entry.id.size(); ++i) {
        const auto high = nibble(digits[i * 2]), low = nibble(digits[i * 2 + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        entry.id[i] = static_cast<std::uint8_t>(high << 4 | low);
    }
    return entry;
}

} // namespace nxloader
