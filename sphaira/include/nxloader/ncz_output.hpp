#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include <zstd.h>

namespace nxloader {

class NczOutput {
public:
    explicit NczOutput(std::size_t limit) : m_limit{limit} {
        assert(limit);
    }

    template <class Flush>
    bool Finish(Flush&& flush) {
        if (!m_used) return true;
        if (!flush(m_buffer, m_used)) return false;
        m_used = 0;
        return true;
    }

    template <class Flush, class Continue, class Produced>
    bool Decompress(ZSTD_DCtx* context, std::span<const std::uint8_t> data,
                    std::size_t& zstd_error, Flush&& flush, Continue&& keep_going,
                    Produced&& produced) {
        ZSTD_inBuffer input{data.data(), data.size(), 0};
        bool drain{};
        do {
            if (!keep_going()) return false;
            const auto room = Writable(ZSTD_DStreamOutSize());
            ZSTD_outBuffer output{room.data(), room.size(), 0};
            const auto result = ZSTD_decompressStream(context, &output, &input);
            if (ZSTD_isError(result)) {
                zstd_error = result;
                return false;
            }
            m_used += output.pos;
            produced(output.pos);
            if (m_used == m_limit && !Finish(flush)) return false;

            // A full output slice may leave decoded bytes inside zstd even after
            // it consumes the input. Drain those before accepting the next block.
            drain = result != 0 && output.pos == output.size;
        } while (input.pos < input.size || drain);
        return true;
    }

    template <class Flush, class Continue, class Produced>
    bool Copy(std::span<const std::uint8_t> data, Flush&& flush,
              Continue&& keep_going, Produced&& produced) {
        while (!data.empty()) {
            if (!keep_going()) return false;
            const auto room = Writable(data.size());
            std::memcpy(room.data(), data.data(), room.size());
            m_used += room.size();
            produced(room.size());
            data = data.subspan(room.size());
            if (m_used == m_limit && !Finish(flush)) return false;
        }
        return true;
    }

private:
    std::span<std::uint8_t> Writable(std::size_t requested) {
        const auto count = std::min(requested, m_limit - m_used);
        const auto needed = m_used + count;
        if (m_buffer.capacity() < m_limit) m_buffer.reserve(m_limit);
        if (m_buffer.size() < needed) m_buffer.resize(needed);
        return {m_buffer.data() + m_used, count};
    }

    std::vector<std::uint8_t> m_buffer;
    std::size_t m_used{};
    const std::size_t m_limit;
};

} // namespace nxloader
