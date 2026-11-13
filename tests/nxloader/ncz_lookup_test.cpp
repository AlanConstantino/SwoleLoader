#include "nxloader/ncz_lookup.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <vector>

struct Block {
    std::uint64_t offset;
    std::uint64_t size;

    bool InRange(std::uint64_t value) const {
        return value >= offset && value - offset < size;
    }
};

const Block* find(const std::vector<Block>& blocks, std::uint64_t offset, std::size_t& cursor) {
    return nxloader::FindSequentialBlock(std::span<const Block>{blocks}, offset, cursor);
}

void boundaries_and_reset() {
    const std::vector<Block> blocks{{0x5000, 13}, {0x500d, 40}, {0x5035, 9}};
    std::size_t cursor{};
    assert(!find(blocks, 0x4fff, cursor));
    for (std::uint64_t offset = 0x5000; offset < 0x503e; ++offset) {
        const auto expected = std::ranges::find_if(blocks, [offset](const auto& block) { return block.InRange(offset); });
        assert(find(blocks, offset, cursor) == &*expected);
    }
    assert(!find(blocks, 0x503e, cursor));
    cursor = 0;
    assert(find(blocks, 0x5000, cursor) == &blocks.front());
    assert(!find({}, 0, cursor));
}

void streaming_chunk_boundaries() {
    const std::vector<Block> blocks{{100, 7}, {107, 32}, {139, 19}, {158, 4}};
    const std::vector<std::uint64_t> chunks{1, 9, 2, 39, 11};
    std::vector<std::uint64_t> consumed(blocks.size());
    std::size_t cursor{};
    std::uint64_t position = blocks.front().offset;
    for (auto bytes : chunks) {
        while (bytes) {
            const auto block = find(blocks, position, cursor);
            assert(block);
            const auto amount = std::min(bytes, block->size - (position - block->offset));
            consumed[static_cast<std::size_t>(block - blocks.data())] += amount;
            position += amount;
            bytes -= amount;
        }
    }
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        assert(consumed[i] == blocks[i].size);
    }
    assert(position == blocks.back().offset + blocks.back().size);
    assert(!find(blocks, position, cursor));
}

void gaps_empty_blocks_and_large_offsets() {
    const std::vector<Block> blocks{{10, 0}, {10, 5}, {20, 0}, {20, 7}};
    std::size_t cursor{};
    assert(find(blocks, 10, cursor) == &blocks[1]);
    assert(!find(blocks, 15, cursor));
    assert(find(blocks, 20, cursor) == &blocks[3]);
    assert(!find(blocks, 27, cursor));

    const auto start = std::numeric_limits<std::uint64_t>::max() - 8;
    const std::vector<Block> large{{start, 8}};
    cursor = 0;
    assert(find(large, start, cursor) == &large.front());
    assert(find(large, start + 7, cursor) == &large.front());
    assert(!find(large, start + 8, cursor));
}

struct CountedBlock : Block {
    std::size_t* comparisons;

    bool InRange(std::uint64_t value) const {
        ++*comparisons;
        return Block::InRange(value);
    }
};

void large_block_table_stays_linear() {
    constexpr std::size_t count = 100000;
    std::size_t comparisons{};
    std::vector<CountedBlock> blocks;
    blocks.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        blocks.push_back({{4096 + i * 17, 17}, &comparisons});
    }
    std::size_t cursor{};
    for (const auto& block : blocks) {
        assert(nxloader::FindSequentialBlock(std::span<const CountedBlock>{blocks}, block.offset, cursor) == &block);
    }
    assert(comparisons <= count * 2);
}

int main() {
    boundaries_and_reset();
    streaming_chunk_boundaries();
    gaps_empty_blocks_and_large_offsets();
    large_block_table_stays_linear();
    std::cout << "NCZ lookup tests passed (100,000-block scaling fixture included).\n";
}
