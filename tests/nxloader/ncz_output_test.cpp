#include "nxloader/ncz_output.hpp"

#include <array>
#include <cassert>
#include <iostream>
#include <memory>

namespace {

constexpr std::size_t window = 4 * 1024 * 1024;
using Bytes = std::vector<std::uint8_t>;
using Context = std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)>;

Bytes Pattern(std::size_t size, bool compressible = false) {
    Bytes data(size);
    std::uint32_t state = 0x39a1e77f;
    for (auto& byte : data) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        byte = compressible ? 0x5a : static_cast<std::uint8_t>(state);
    }
    return data;
}

Bytes Compress(const Bytes& source) {
    Bytes encoded(ZSTD_compressBound(source.size()));
    const auto size = ZSTD_compress(encoded.data(), encoded.size(), source.data(), source.size(), 1);
    assert(!ZSTD_isError(size));
    encoded.resize(size);
    return encoded;
}

struct Sink {
    Bytes bytes;
    std::vector<std::size_t> writes;
    std::array<Bytes, 4> recycled;
    std::size_t slot{};
    std::size_t produced{};

    bool Flush(Bytes& buffer, std::size_t size) {
        assert(size && size <= window && buffer.size() >= size);
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + size);
        writes.push_back(size);
        // Match SetWriteBuf's ownership transfer; recycled slots may be empty,
        // contain a short first/header buffer, or contain a full older buffer.
        buffer.resize(size);
        std::swap(buffer, recycled[slot++ % recycled.size()]);
        return true;
    }
};

void Feed(nxloader::NczOutput& output, ZSTD_DCtx* context, const Bytes& encoded,
          Sink& sink, std::span<const std::size_t> fragments) {
    std::size_t position{}, fragment{};
    while (position < encoded.size()) {
        const auto size = std::min(fragments[fragment++ % fragments.size()], encoded.size() - position);
        std::size_t error{};
        assert(output.Decompress(context, std::span{encoded}.subspan(position, size), error,
            [&](auto& data, auto count) { return sink.Flush(data, count); },
            [] { return true; }, [&](auto count) { sink.produced += count; }));
        assert(!error);
        position += size;
    }
}

void CheckWrites(const Sink& sink, const Bytes& expected) {
    assert(sink.bytes == expected && sink.produced == expected.size());
    for (std::size_t i = 0; i + 1 < sink.writes.size(); ++i) assert(sink.writes[i] == window);
    assert(sink.writes.back() == (expected.size() % window ? expected.size() % window : window));
}

void streaming_and_fragmented_frames() {
    constexpr std::array fragments{std::size_t{1}, std::size_t{7}, std::size_t{131071}, window};
    for (const auto size : {window - 1, window, window + 1, 2 * window + 777}) {
        for (const bool compressible : {false, true}) {
            const auto expected = Pattern(size, compressible);
            const auto encoded = Compress(expected);
            Context context(ZSTD_createDCtx(), ZSTD_freeDCtx);
            assert(context);
            nxloader::NczOutput output(window);
            Sink sink;
            Feed(output, context.get(), encoded, sink, fragments);
            assert(output.Finish([&](auto& data, auto count) { return sink.Flush(data, count); }));
            CheckWrites(sink, expected);
        }
    }
}

void drains_zstd_after_tiny_remaining_space() {
    auto expected = Pattern(window - 1);
    const auto compressed_part = Pattern(window + 73, true);
    const auto encoded = Compress(compressed_part);
    Context context(ZSTD_createDCtx(), ZSTD_freeDCtx);
    nxloader::NczOutput output(window);
    Sink sink;
    const auto flush = [&](auto& data, auto count) { return sink.Flush(data, count); };
    const auto produced = [&](auto count) { sink.produced += count; };
    assert(output.Copy(expected, flush, [] { return true; }, produced));
    const std::array fragments{encoded.size()};
    Feed(output, context.get(), encoded, sink, fragments);
    assert(output.Finish(flush));
    expected.insert(expected.end(), compressed_part.begin(), compressed_part.end());
    CheckWrites(sink, expected);
}

void mixed_raw_and_independent_compressed_blocks() {
    constexpr std::array fragments{std::size_t{3}, std::size_t{65521}, window};
    Context context(ZSTD_createDCtx(), ZSTD_freeDCtx);
    nxloader::NczOutput output(window);
    Sink sink;
    sink.recycled[0].resize(0x4000);
    sink.recycled[1].resize(window);
    Bytes expected;
    for (std::size_t block = 0; block < 7; ++block) {
        const auto plain = Pattern(block % 2 ? window + 17 : window / 3 + block, block % 3 == 0);
        expected.insert(expected.end(), plain.begin(), plain.end());
        if (block % 2) {
            assert(output.Copy(plain, [&](auto& data, auto count) { return sink.Flush(data, count); },
                [] { return true; }, [&](auto count) { sink.produced += count; }));
        } else {
            Feed(output, context.get(), Compress(plain), sink, fragments);
        }
    }
    assert(output.Finish([&](auto& data, auto count) { return sink.Flush(data, count); }));
    CheckWrites(sink, expected);
}

void errors_and_cancellation_propagate() {
    const auto encoded = Compress(Pattern(2 * window + 1, true));
    for (const bool raw : {false, true}) {
        nxloader::NczOutput output(window);
        Context context(ZSTD_createDCtx(), ZSTD_freeDCtx);
        std::size_t attempts{}, produced{}, error{};
        const auto flush = [&](auto&, auto) { ++attempts; return false; };
        const auto count = [&](auto amount) { produced += amount; };
        const bool ok = raw
            ? output.Copy(Pattern(2 * window), flush, [] { return true; }, count)
            : output.Decompress(context.get(), encoded, error, flush, [] { return true; }, count);
        assert(!ok && attempts == 1 && produced == window && !error);
    }
    {
        nxloader::NczOutput output(window);
        Context context(ZSTD_createDCtx(), ZSTD_freeDCtx);
        std::size_t checks{}, produced{}, error{};
        assert(!output.Decompress(context.get(), encoded, error, [](auto&, auto) { return true; },
            [&] { return ++checks < 4; }, [&](auto amount) { produced += amount; }));
        assert(checks == 4 && produced && produced < window && !error);
    }
    {
        nxloader::NczOutput output(window);
        Context context(ZSTD_createDCtx(), ZSTD_freeDCtx);
        std::size_t error{}, produced{};
        assert(!output.Decompress(context.get(), Pattern(128), error,
            [](auto&, auto) { return true; }, [] { return true; }, [&](auto count) { produced += count; }));
        assert(ZSTD_isError(error) && !produced);
    }
    {
        nxloader::NczOutput output(window);
        std::size_t attempts{};
        assert(output.Copy(Pattern(11), [](auto&, auto) { return true; },
            [] { return true; }, [](auto) {}));
        assert(!output.Finish([&](auto&, auto size) { ++attempts; assert(size == 11); return false; }));
        assert(attempts == 1);
    }
}

} // namespace

int main() {
    streaming_and_fragmented_frames();
    drains_zstd_after_tiny_remaining_space();
    mixed_raw_and_independent_compressed_blocks();
    errors_and_cancellation_propagate();
    std::cout << "NCZ output tests passed: real zstd, boundary drain, recycled buffers, raw/compressed blocks, errors and cancellation.\n";
}
