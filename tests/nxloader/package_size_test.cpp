#include "nxloader/package_size.hpp"

#include <cassert>
#include <iostream>
#include <map>

using Bytes = std::vector<std::uint8_t>;
constexpr std::uint64_t MiB = 1024 * 1024;
constexpr std::uint64_t GiB = 1024 * MiB;

void Put32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.at(at + i) = value >> (i * 8);
}

void Put64(Bytes& bytes, std::size_t at, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) bytes.at(at + i) = value >> (i * 8);
}

void Magic(Bytes& bytes, std::size_t at, std::string_view value) {
    std::copy(value.begin(), value.end(), bytes.begin() + at);
}

struct SparseFile {
    std::uint64_t size{};
    std::map<std::uint64_t, Bytes> pieces;
    std::uint64_t bytes_read{};
    std::size_t calls{};
    bool fail{};

    bool Read(std::uint64_t offset, std::span<std::uint8_t> output) {
        ++calls;
        bytes_read += output.size();
        assert(bytes_read <= nxloader::package_size_detail::MetadataBudget);
        assert(offset <= size && output.size() <= size - offset);
        if (fail) return false;
        std::fill(output.begin(), output.end(), 0);
        for (const auto& [where, bytes] : pieces) {
            const auto begin = std::max(offset, where);
            const auto end = std::min(offset + output.size(), where + bytes.size());
            if (begin < end) std::copy(bytes.begin() + (begin - where), bytes.begin() + (end - where),
                output.begin() + (begin - offset));
        }
        return true;
    }

    void Add(std::uint64_t offset, const SparseFile& other) {
        for (const auto& [where, bytes] : other.pieces) pieces[offset + where] = bytes;
    }
};

struct Member {
    std::string name;
    SparseFile contents;
};

SparseFile Plain(std::uint64_t size) {
    return {size, {}, 0, 0, false};
}

SparseFile Ncz(std::uint64_t expanded, bool block = false) {
    assert(expanded > 0x4000);
    SparseFile result;
    Bytes headers(16 + 64 + (block ? 24 : 8));
    Magic(headers, 0, "NCZSECTN");
    Put64(headers, 8, 1);
    Put64(headers, 16, 0x4000);
    Put64(headers, 24, expanded - 0x4000);
    if (block) {
        Magic(headers, 80, "NCZBLOCK");
        headers[88] = 2;
        headers[89] = 1;
        headers[91] = 22;
        const auto blocks = (expanded - 0x4000) / (4 * MiB) + ((expanded - 0x4000) % (4 * MiB) != 0);
        Put32(headers, 92, blocks);
        Put64(headers, 96, expanded - 0x4000);
        result.size = 0x4000 + headers.size() + blocks * 4 + blocks * 32;
    } else {
        Put32(headers, 80, 0xFD2FB528);
        result.size = 0x4000 + headers.size() + 32;
    }
    result.pieces[0x4000] = std::move(headers);
    return result;
}

SparseFile Table(std::vector<Member> members, bool hfs = false) {
    const std::size_t entry_size = hfs ? 64 : 24;
    const auto table_size = members.size() * entry_size;
    std::string strings;
    for (const auto& member : members) strings += member.name + '\0';
    Bytes metadata(16 + table_size + strings.size());
    Magic(metadata, 0, hfs ? "HFS0" : "PFS0");
    Put32(metadata, 4, members.size());
    Put32(metadata, 8, strings.size());
    std::copy(strings.begin(), strings.end(), metadata.begin() + 16 + table_size);
    SparseFile result;
    result.size = metadata.size();
    std::size_t name_offset{};
    std::uint64_t data_offset{};
    for (std::size_t i = 0; i < members.size(); ++i) {
        const auto at = 16 + i * entry_size;
        Put64(metadata, at, data_offset);
        Put64(metadata, at + 8, members[i].contents.size);
        Put32(metadata, at + 16, name_offset);
        result.Add(metadata.size() + data_offset, members[i].contents);
        name_offset += members[i].name.size() + 1;
        data_offset += members[i].contents.size;
        result.size += members[i].contents.size;
    }
    result.pieces[0] = std::move(metadata);
    return result;
}

nxloader::PackageSize Estimate(SparseFile& file, std::string_view path = "game.nsp") {
    file.bytes_read = file.calls = 0;
    return nxloader::EstimatePackageSize(path, file.size,
        [&](std::uint64_t offset, std::span<std::uint8_t> output) { return file.Read(offset, output); },
        [] { return false; });
}

void AssertUnknown(SparseFile& file, std::string_view path = "game.nsp", std::uint64_t partial = 0, std::uint64_t count = 0) {
    const auto estimate = Estimate(file, path);
    assert(!estimate.known && !estimate.error.empty());
    assert(estimate.source_bytes == file.size && estimate.installed_bytes == partial && estimate.content_count == count);
}

void ordinary_packages_are_sparse_reads() {
    auto file = Table({{"base.nca", Plain(30 * GiB)}, {"control.nca", Plain(2 * MiB)},
        {"title.cnmt.nca", Plain(4096)}, {"rights.tik", Plain(1024)}});
    const auto estimate = Estimate(file, "UPPER.NSP");
    assert(estimate.known && estimate.error.empty());
    assert(estimate.content_count == 3);
    assert(estimate.installed_bytes == 30 * GiB + 2 * MiB + 4096);
    assert(estimate.source_bytes == file.size);
    assert(file.bytes_read < 1024);
}

void nsz_expansion_and_block_layout() {
    for (const bool block : {false, true}) {
        auto file = Table({{"base.ncz", Ncz(8 * GiB + 0x4000, block)},
            {"meta.cnmt.nca", Plain(0x4000)}});
        const auto estimate = Estimate(file, "game.nsz");
        assert(estimate.known && estimate.content_count == 2);
        assert(estimate.installed_bytes == 8 * GiB + 0x8000);
        assert(estimate.installed_bytes > estimate.source_bytes * 1000);
        assert(file.bytes_read < 1024);
    }
    auto disguised = Table({{"base.nca", Ncz(4 * GiB)}});
    assert(Estimate(disguised).installed_bytes == 4 * GiB);
}

void xci_secure_partition_and_key_area() {
    for (const std::uint64_t offset : {0xF000, 0x10000}) {
        for (const bool compressed : {false, true}) {
            auto secure = Table({{compressed ? "base.ncz" : "base.nca",
                compressed ? Ncz(3 * GiB, true) : Plain(3 * GiB)}, {"meta.cnmt.nca", Plain(4096)}}, true);
            auto root = Table({{"update", Plain(2 * GiB)}, {"normal", Plain(32)}, {"secure", secure}}, true);
            SparseFile file;
            file.size = offset + root.size;
            file.Add(offset, root);
            const auto estimate = Estimate(file, compressed ? "game.xcz" : "game.xci");
            assert(estimate.known && estimate.content_count == 2);
            assert(estimate.installed_bytes == 3 * GiB + 4096);
            assert(file.bytes_read < 2048);
        }
    }
}

void malformed_container_metadata() {
    auto bad = Table({{"base.nca", Plain(MiB)}});
    bad.size = 12;
    AssertUnknown(bad);

    bad = Table({{"base.nca", Plain(MiB)}});
    Put32(bad.pieces[0], 4, 0xFFFFFFFF);
    AssertUnknown(bad);
    assert(bad.bytes_read == 16);

    bad = Table({{"base.nca", Plain(MiB)}});
    Put32(bad.pieces[0], 8, 5 * MiB);
    AssertUnknown(bad);
    assert(bad.bytes_read == 16);

    bad = Table({{"base.nca", Plain(MiB)}});
    Put32(bad.pieces[0], 16 + 16, 1000);
    AssertUnknown(bad);

    bad = Table({{"base.nca", Plain(MiB)}});
    bad.pieces[0].back() = 'x';
    AssertUnknown(bad);

    bad = Table({{"base.nca", Plain(MiB)}});
    Put64(bad.pieces[0], 16, std::numeric_limits<std::uint64_t>::max());
    AssertUnknown(bad);

    bad = Table({{"base.nca", Plain(MiB)}});
    Put64(bad.pieces[0], 24, std::numeric_limits<std::uint64_t>::max());
    AssertUnknown(bad);

    bad = Table({{"base.nca", Plain(MiB)}});
    --bad.size;
    AssertUnknown(bad);

    bad = Table({{"base.nca", Plain(MiB)}, {"Base.NCA", Plain(MiB)}});
    AssertUnknown(bad);

    bad = Table({{"../base.nca", Plain(MiB)}});
    AssertUnknown(bad);

    bad = Table({{"one.nca", Plain(MiB)}, {"two.nca", Plain(MiB)}});
    Put64(bad.pieces[0], 16 + 24, MiB - 1);
    AssertUnknown(bad);

    bad = Table({{"readme.txt", Plain(1024)}});
    AssertUnknown(bad);
    bad = Plain(16);
    AssertUnknown(bad, "mod.msp");
    assert(bad.calls == 0);
}

void invalid_ncz_is_never_compressed_size() {
    auto file = Table({{"base.ncz", Plain(MiB)}});
    AssertUnknown(file, "game.nsz");

    auto content = Ncz(GiB);
    Put64(content.pieces[0x4000], 8, 4097);
    file = Table({{"base.ncz", content}});
    AssertUnknown(file, "game.nsz");

    const auto enormous = std::uint64_t(std::numeric_limits<std::int64_t>::max() / 2) + MiB;
    file = Table({{"one.ncz", Ncz(enormous)}, {"two.ncz", Ncz(enormous)}});
    AssertUnknown(file, "game.nsz", enormous, 1);
}

void multiple_sections_and_partition_boundaries() {
    auto content = Ncz(3 * GiB);
    Bytes headers(16 + 2 * 64 + 8);
    Magic(headers, 0, "NCZSECTN");
    Put64(headers, 8, 2);
    Put64(headers, 16, 0x4000);
    Put64(headers, 24, GiB - 0x4000);
    Put64(headers, 80, GiB);
    Put64(headers, 88, 2 * GiB);
    Put32(headers, 144, 0xFD2FB528);
    content.pieces[0x4000] = headers;
    content.size = 0x4000 + headers.size() + 32;
    auto file = Table({{"base.ncz", content}});
    assert(Estimate(file, "game.nsz").installed_bytes == 3 * GiB);

    Put64(content.pieces[0x4000], 80, GiB - 1);
    file = Table({{"base.ncz", content}});
    AssertUnknown(file, "game.nsz");

    auto secure = Table({{"base.nca", Plain(MiB)}}, true);
    Put64(secure.pieces[0], 16 + 8, MiB + 1);
    auto root = Table({{"secure", secure}, {"normal", Plain(MiB)}}, true);
    SparseFile xci;
    xci.size = 0xF000 + root.size;
    xci.Add(0xF000, root);
    AssertUnknown(xci, "game.xci");

    root = Table({{"normal", secure}}, true);
    xci = {};
    xci.size = 0xF000 + root.size;
    xci.Add(0xF000, root);
    AssertUnknown(xci, "game.xci");

    content = Ncz(GiB);
    Put64(content.pieces[0x4000], 24, std::numeric_limits<std::uint64_t>::max());
    file = Table({{"base.ncz", content}});
    AssertUnknown(file, "game.nsz");

    content = Ncz(GiB);
    content.size = 0x4000 + 16 + 63;
    file = Table({{"base.ncz", content}});
    AssertUnknown(file, "game.nsz");

    content = Ncz(GiB, true);
    Put64(content.pieces[0x4000], 96, GiB - 0x4000 - 1);
    file = Table({{"base.ncz", content}});
    AssertUnknown(file, "game.nsz");

    content = Ncz(GiB, true);
    Put32(content.pieces[0x4000], 92, 1);
    file = Table({{"base.ncz", content}});
    AssertUnknown(file, "game.nsz");

    content = Ncz(GiB, true);
    content.size = 0x4000 + 16 + 64 + 24;
    file = Table({{"base.ncz", content}});
    AssertUnknown(file, "game.nsz");

    content = Ncz(GiB);
    content.pieces[0x4000][80] = 0;
    file = Table({{"base.ncz", content}});
    AssertUnknown(file, "game.nsz");
}

void read_failures_and_cancellation() {
    auto file = Table({{"base.ncz", Ncz(GiB)}, {"metadata.cnmt.nca", Plain(4096)}});
    file.fail = true;
    AssertUnknown(file, "game.nsz");
    file.fail = false;
    std::size_t checks{};
    const auto baseline = nxloader::EstimatePackageSize("game.nsz", file.size,
        [&](std::uint64_t off, std::span<std::uint8_t> out) { return file.Read(off, out); },
        [&] { ++checks; return false; });
    assert(baseline.known);
    for (std::size_t stop = 0; stop < checks; ++stop) {
        file.bytes_read = file.calls = 0;
        std::size_t current{};
        const auto result = nxloader::EstimatePackageSize("game.nsz", file.size,
            [&](std::uint64_t off, std::span<std::uint8_t> out) { return file.Read(off, out); },
            [&] { return current++ == stop; });
        assert(!result.known && result.installed_bytes <= baseline.installed_bytes && !result.error.empty());
    }
}

void original_header_fallback_and_partial_sizes() {
    auto content = Ncz(8 * GiB, true);
    Put64(content.pieces[0x4000], 96, 17); // compression metadata disagrees
    auto file = Table({{"base.ncz", content}});
    AssertUnknown(file, "game.nsz");
    file.bytes_read = 0;
    const auto recovered = nxloader::EstimatePackageSize("game.nsz", file.size,
        [&](std::uint64_t off, std::span<std::uint8_t> out) { return file.Read(off, out); },
        [] { return false; }, [](std::span<const std::uint8_t> header) {
            assert(header.size() == 0xC00); return 8 * GiB;
        });
    assert(recovered.known && recovered.installed_bytes == 8 * GiB && file.bytes_read < 4096);
    auto mixed = Table({{"good.nca", Plain(GiB)}, {"bad.ncz", Plain(MiB)}});
    AssertUnknown(mixed, "mixed.nsz", GiB, 1);
    assert(Estimate(mixed, "mixed.nsz").error == "Invalid NCZ section header.");
}

int main() {
    original_header_fallback_and_partial_sizes();
    ordinary_packages_are_sparse_reads();
    nsz_expansion_and_block_layout();
    xci_secure_partition_and_key_area();
    malformed_container_metadata();
    invalid_ncz_is_never_compressed_size();
    multiple_sections_and_partition_boundaries();
    read_failures_and_cancellation();
    std::cout << "Package size tests passed: bounded sparse NSP/NSZ/XCI/XCZ metadata, expansion, invalid input and cancellation.\n";
}
