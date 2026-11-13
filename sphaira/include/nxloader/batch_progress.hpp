#pragma once

#include "nxloader/transfer_rate.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace nxloader {

// Progress across a whole list, such as a batch install, built from the
// progress each item already reports. An item can run several transfers, one
// per content file, and they add up until the next item starts.
class BatchProgress {
public:
    void Start(std::uint64_t now_ns, std::int64_t total) {
        *this = {};
        m_start_ns = now_ns;
        m_total = std::max<std::int64_t>(0, total);
        m_started = true;
    }

    // The previous item counts as finished, whatever it reported, so a failed
    // or skipped item doesn't leave the bar short.
    void NextItem(std::int64_t size) {
        m_done = std::min(m_total, m_done + m_item_size);
        m_item_size = std::max<std::int64_t>(0, size);
        m_item_done = 0;
    }

    // A transfer inside the current item ended at offset.
    void EndTransfer(std::int64_t offset) {
        m_item_done = ItemOffset(offset);
    }

    bool Started() const {
        return m_started;
    }

    std::int64_t Size() const {
        return m_total;
    }

    std::int64_t Offset(std::int64_t transfer_offset) const {
        return std::min(m_total, m_done + ItemOffset(transfer_offset));
    }

    // Averaged over the whole run, so the time spent between items, opening
    // files and registering content, is part of the estimate.
    std::uint64_t BytesPerSecond(std::uint64_t now_ns, std::int64_t transfer_offset) const {
        if (now_ns < m_start_ns + 1'000'000'000) return 0;
        return static_cast<std::uint64_t>(static_cast<double>(Offset(transfer_offset)) * 1'000'000'000.0
            / static_cast<double>(now_ns - m_start_ns));
    }

    std::optional<std::uint64_t> SecondsRemaining(std::uint64_t now_ns, std::int64_t transfer_offset) const {
        return TransferSecondsRemaining(m_total, Offset(transfer_offset), BytesPerSecond(now_ns, transfer_offset));
    }

private:
    std::int64_t ItemOffset(std::int64_t transfer_offset) const {
        return std::clamp<std::int64_t>(m_item_done + std::max<std::int64_t>(0, transfer_offset), 0, m_item_size);
    }

    std::uint64_t m_start_ns{};
    std::int64_t m_total{};
    std::int64_t m_done{};
    std::int64_t m_item_size{};
    std::int64_t m_item_done{};
    bool m_started{};
};

} // namespace nxloader
