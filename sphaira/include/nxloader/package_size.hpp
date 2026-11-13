#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <type_traits>
#include <vector>

namespace nxloader {

struct PackageSize {
    std::uint64_t source_bytes{};
    std::uint64_t installed_bytes{};
    std::uint64_t content_count{};
    bool known{};
    std::string error;
    bool source_known{true};
};

namespace package_size_detail {

constexpr std::uint64_t MetadataBudget = 4 * 1024 * 1024;
constexpr std::uint64_t MaxMembers = 65536;
constexpr std::uint64_t MaxSections = 4096;
constexpr std::uint64_t NcaPrefix = 0x4000;
constexpr std::uint64_t MaxSize = std::numeric_limits<std::int64_t>::max();

inline std::uint32_t U32(const std::uint8_t* bytes) {
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8)
        | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

inline std::uint64_t U64(const std::uint8_t* bytes) {
    return U32(bytes) | (std::uint64_t(U32(bytes + 4)) << 32);
}

inline bool Add(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (a > MaxSize || b > MaxSize - a) return false;
    out = a + b;
    return true;
}

inline std::string Lower(std::string_view value) {
    std::string result(value);
    for (auto& c : result) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    return result;
}

struct Member {
    std::string name;
    std::uint64_t offset{};
    std::uint64_t size{};
};

struct NoContentHeader {};

template <class ReadAt, class Cancelled, class ContentHeader = NoContentHeader>
class Reader {
public:
    Reader(std::uint64_t file_size, ReadAt& read_at, Cancelled& cancelled, ContentHeader& content_header)
        : m_file_size(file_size), m_read_at(read_at), m_cancelled(cancelled), m_content_header(content_header) {}

    bool Fail(const char* message) {
        if (error.empty()) error = message;
        return false;
    }

    bool Continue() {
        return !m_cancelled() || Fail("Size calculation cancelled.");
    }

    bool Read(std::uint64_t offset, std::span<std::uint8_t> bytes) {
        if (!Continue()) return false;
        if (offset > m_file_size || bytes.size() > m_file_size - offset)
            return Fail("Truncated package metadata.");
        if (bytes.size() > m_budget) return Fail("Package metadata exceeds the size-estimation limit.");
        m_budget -= bytes.size();
        if (!bytes.empty() && !m_read_at(offset, bytes)) return Fail("Could not read package metadata.");
        return Continue();
    }

    bool Table(std::uint64_t offset, std::uint64_t size, bool hfs,
               std::vector<Member>& members) {
        std::uint64_t end{};
        if (!Add(offset, size, end) || end > m_file_size || size < 16)
            return Fail("Invalid package partition bounds.");
        std::array<std::uint8_t, 16> header{};
        if (!Read(offset, header)) return false;
        if (std::memcmp(header.data(), hfs ? "HFS0" : "PFS0", 4))
            return Fail("Unsupported package header.");
        const auto count = U32(header.data() + 4);
        const auto strings_size = U32(header.data() + 8);
        const std::uint64_t entry_size = hfs ? 64 : 24;
        const auto table_size = std::uint64_t(count) * entry_size;
        const auto metadata_size = table_size + strings_size;
        if (count > MaxMembers || metadata_size > m_budget)
            return Fail("Package metadata exceeds the size-estimation limit.");
        if (metadata_size > size - header.size()) return Fail("Truncated package file table.");
        const auto data_offset = offset + header.size() + metadata_size;
        std::vector<std::uint8_t> metadata(static_cast<std::size_t>(metadata_size));
        if (!Read(offset + header.size(), metadata)) return false;

        members.clear();
        members.reserve(count);
        std::set<std::string> names;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
        ranges.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!Continue()) return false;
            const auto* entry = metadata.data() + i * entry_size;
            const auto name_offset = U32(entry + 16);
            if (name_offset >= strings_size) return Fail("Invalid package filename offset.");
            const auto* name = reinterpret_cast<const char*>(metadata.data() + table_size + name_offset);
            const auto* nul = static_cast<const char*>(std::memchr(name, 0, strings_size - name_offset));
            if (!nul || nul == name || nul - name > 255) return Fail("Invalid package filename.");
            const std::string_view name_view(name, static_cast<std::size_t>(nul - name));
            if (name_view == "." || name_view == ".." || name_view.find_first_of("/\\:") != name_view.npos)
                return Fail("Invalid package filename.");
            auto normalized = Lower(name_view);
            if (!names.insert(normalized).second) return Fail("Duplicate package filename.");

            std::uint64_t begin{}, member_end{};
            const auto member_size = U64(entry + 8);
            if (!Add(data_offset, U64(entry), begin) || !Add(begin, member_size, member_end) || member_end > end)
                return Fail("Package member extends beyond its partition.");
            members.push_back({std::move(normalized), begin, member_size});
            if (member_size) ranges.emplace_back(begin, member_end);
        }
        std::sort(ranges.begin(), ranges.end());
        for (std::size_t i = 1; i < ranges.size(); ++i) {
            if (!Continue()) return false;
            if (ranges[i].first < ranges[i - 1].second) return Fail("Overlapping package members.");
        }
        return Continue();
    }

    bool Xci(std::vector<Member>& contents) {
        std::array<std::uint8_t, 4> magic{};
        std::uint64_t root{};
        for (const std::uint64_t candidate : {0xF000, 0x10000}) {
            if (candidate > m_file_size || magic.size() > m_file_size - candidate) continue;
            if (!Read(candidate, magic)) return false;
            if (!std::memcmp(magic.data(), "HFS0", magic.size())) {
                root = candidate;
                break;
            }
        }
        if (!root) return Fail("XCI root partition was not found.");
        std::vector<Member> partitions;
        if (!Table(root, m_file_size - root, true, partitions)) return false;
        for (const auto& partition : partitions) {
            if (partition.name == "secure") return Table(partition.offset, partition.size, true, contents);
        }
        return Fail("XCI secure partition was not found.");
    }

    bool ContentSize(const Member& member, std::uint64_t& expanded) {
        if (member.size < 0xC00) return Fail("Content is too short for an NCA header.");
        const bool ncz_name = member.name.ends_with(".ncz");
        const auto original_size = [&] {
            if constexpr (!std::is_same_v<ContentHeader, NoContentHeader>) {
                std::array<std::uint8_t, 0xC00> original{};
                if (!Read(member.offset, original)) return false;
                const auto size = m_content_header(std::span<const std::uint8_t>(original));
                if (size >= NcaPrefix && size <= MaxSize) { expanded = size; return true; }
            }
            return false;
        };
        if (ncz_name && original_size()) return true;
        std::array<std::uint8_t, 16> header{};
        if (member.size < NcaPrefix + header.size()) {
            if (ncz_name) return Fail("Truncated NCZ header.");
            expanded = member.size;
            return true;
        }
        if (!Read(member.offset + NcaPrefix, header)) return false;
        if (std::memcmp(header.data(), "NCZSECTN", 8)) {
            if (ncz_name) return Fail("Invalid NCZ section header.");
            expanded = member.size;
            return true;
        }
        if (!ncz_name && original_size()) return true;

        const auto count = U64(header.data() + 8);
        if (!count || count > MaxSections) return Fail("Invalid NCZ section count.");
        const auto sections_size = count * 64;
        const auto sections_offset = NcaPrefix + header.size();
        if (sections_size > member.size - sections_offset) return Fail("Truncated NCZ section table.");
        if (sections_size > m_budget) return Fail("Package metadata exceeds the size-estimation limit.");
        std::vector<std::uint8_t> sections(static_cast<std::size_t>(sections_size));
        if (!Read(member.offset + sections_offset, sections)) return false;
        std::uint64_t previous_end = NcaPrefix;
        for (std::uint64_t i = 0; i < count; ++i) {
            if (!Continue()) return false;
            const auto* section = sections.data() + i * 64;
            const auto offset = U64(section);
            const auto section_size = U64(section + 8);
            std::uint64_t end{};
            if (!section_size || offset < previous_end || !Add(offset, section_size, end))
                return Fail("Invalid NCZ section bounds.");
            previous_end = end;
        }
        expanded = previous_end;

        const auto payload_offset = sections_offset + sections_size;
        if (member.size - payload_offset < 8) return Fail("Truncated NCZ payload header.");
        std::array<std::uint8_t, 24> block{};
        if (!Read(member.offset + payload_offset, std::span(block).first(8))) return false;
        if (std::memcmp(block.data(), "NCZBLOCK", 8)) {
            const auto magic = U32(block.data());
            if (magic != 0xFD2FB528 && (magic & 0xFFFFFFF0) != 0x184D2A50)
                return Fail("Unsupported NCZ compression header.");
            return true;
        }
        if (member.size - payload_offset < block.size()) return Fail("Truncated NCZ block header.");
        if (!Read(member.offset + payload_offset + 8, std::span(block).subspan(8))) return false;
        const auto exponent = block[11];
        const auto blocks = U32(block.data() + 12);
        const auto body_size = U64(block.data() + 16);
        if (block[8] != 2 || block[9] != 1 || exponent < 14 || exponent > 32 || !blocks || !body_size)
            return Fail("Invalid NCZ block layout.");
        std::uint64_t total{};
        if (!Add(NcaPrefix, body_size, total) || total != expanded)
            return Fail("NCZ expanded-size metadata disagrees.");
        const auto block_size = std::uint64_t{1} << exponent;
        if (body_size / block_size + (body_size % block_size != 0) != blocks)
            return Fail("Invalid NCZ block count.");
        const auto table_size = std::uint64_t(blocks) * 4;
        const auto remaining = member.size - payload_offset - block.size();
        if (table_size > remaining || remaining - table_size < blocks)
            return Fail("Truncated NCZ block table or payload.");
        return true;
    }

    std::string error;

private:
    std::uint64_t m_file_size;
    ReadAt& m_read_at;
    Cancelled& m_cancelled;
    ContentHeader& m_content_header;
    std::uint64_t m_budget{MetadataBudget};
};

} // namespace package_size_detail

// Reads only bounded container tables and small NCZ headers. The estimate assumes
// all content is new; it excludes filesystem/database overhead and is not a
// package-integrity check. Unknown results must not be treated as zero required space.
template <class ReadAt, class Cancelled, class ContentHeader = package_size_detail::NoContentHeader>
PackageSize EstimatePackageSize(std::string_view path, std::uint64_t file_size,
                               ReadAt&& read_at, Cancelled&& cancelled, ContentHeader content_header = {}) {
    using namespace package_size_detail;
    PackageSize result;
    result.source_bytes = file_size;
    Reader reader(file_size, read_at, cancelled, content_header);
    const auto finish_unknown = [&] {
        result.error = reader.error;
        return result;
    };
    if (!reader.Continue()) return finish_unknown();
    if (file_size > MaxSize) {
        reader.Fail("Package size exceeds the filesystem limit.");
        return finish_unknown();
    }
    const auto extension = Lower(path.substr(path.find_last_of('.') + 1));
    std::vector<Member> members;
    if (extension == "nsp" || extension == "nsz") {
        if (!reader.Table(0, file_size, false, members)) return finish_unknown();
    } else if (extension == "xci" || extension == "xcz") {
        if (!reader.Xci(members)) return finish_unknown();
    } else {
        reader.Fail("Installed size is unavailable for this package format.");
        return finish_unknown();
    }
    for (const auto& member : members) {
        if (!reader.Continue()) return finish_unknown();
        if (!member.name.ends_with(".nca") && !member.name.ends_with(".ncz")) continue;
        std::uint64_t expanded{};
        if (!reader.ContentSize(member, expanded)) {
            if (result.error.empty()) result.error = reader.error;
            reader.error.clear();
            continue;
        }
        if (!Add(result.installed_bytes, expanded, result.installed_bytes)) {
            reader.Fail("Installed size exceeds the filesystem limit.");
            return finish_unknown();
        }
        ++result.content_count;
    }
    if (!result.content_count) {
        if (!result.error.empty()) return result;
        reader.Fail("No installable content was found in the package.");
        return finish_unknown();
    }
    result.known = result.error.empty();
    return result;
}

} // namespace nxloader
