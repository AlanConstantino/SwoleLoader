#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace nxloader {

class TransferRate {
public:
    void Reset(std::uint64_t now_ns, std::int64_t offset = 0) {
        m_sample_ns = now_ns;
        m_sample_offset = m_observed_offset = std::max<std::int64_t>(0, offset);
        m_bytes_per_second = 0;
    }

    void Update(std::uint64_t now_ns, std::int64_t offset) {
        offset = std::max<std::int64_t>(0, offset);
        if (now_ns < m_sample_ns || offset < m_observed_offset) {
            Reset(now_ns, offset);
            return;
        }
        m_observed_offset = offset;
        const auto elapsed_ns = now_ns - m_sample_ns;
        if (elapsed_ns < 1'000'000'000) return;

        const auto bytes = offset - m_sample_offset;
        m_bytes_per_second = static_cast<std::uint64_t>(
            static_cast<double>(bytes) * 1'000'000'000.0 / static_cast<double>(elapsed_ns));
        m_sample_ns = now_ns;
        m_sample_offset = offset;
    }

    std::uint64_t BytesPerSecond() const {
        return m_bytes_per_second;
    }

private:
    std::uint64_t m_sample_ns{};
    std::int64_t m_sample_offset{};
    std::int64_t m_observed_offset{};
    std::uint64_t m_bytes_per_second{};
};

inline std::optional<std::uint64_t> TransferSecondsRemaining(
    std::int64_t size, std::int64_t offset, std::uint64_t bytes_per_second) {
    if (size <= 0 || offset < 0) return std::nullopt;
    if (offset >= size) return 0;
    if (!bytes_per_second) return std::nullopt;
    const auto remaining = static_cast<std::uint64_t>(size - offset);
    return remaining / bytes_per_second + (remaining % bytes_per_second != 0);
}

} // namespace nxloader
