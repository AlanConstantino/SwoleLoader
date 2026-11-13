#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace nxloader {

// Compressed offsets advance monotonically during installation. Start a new
// cursor at zero for each NCA; no blocks before that cursor are searched again.
template<class Block>
const Block* FindSequentialBlock(std::span<const Block> blocks, std::uint64_t offset, std::size_t& cursor) {
    while (cursor < blocks.size()) {
        const auto& block = blocks[cursor];
        if (block.InRange(offset)) {
            return &block;
        }
        if (offset < block.offset) {
            return nullptr;
        }
        ++cursor;
    }
    return nullptr;
}

}
