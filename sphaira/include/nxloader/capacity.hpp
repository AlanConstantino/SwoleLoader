#pragma once

#include "nxloader/package_size.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace nxloader {

struct StorageSpace {
    std::uint64_t free_bytes{};
    std::uint64_t total_bytes{};
    bool known{};
};

namespace capacity_detail {

inline bool Add(std::uint64_t& value, std::uint64_t amount) {
    if (amount > std::numeric_limits<std::uint64_t>::max() - value) return false;
    value += amount;
    return true;
}

inline bool Valid(StorageSpace space) {
    return space.known && space.total_bytes && space.free_bytes <= space.total_bytes;
}

} // namespace capacity_detail

struct CapacityEstimate {
    std::uint64_t source_bytes{};
    std::uint64_t installed_bytes{};
    std::uint64_t content_count{};
    std::size_t unknown_count{};
    bool overflow{};

    void Add(const PackageSize& package) {
        overflow |= !capacity_detail::Add(source_bytes, package.source_bytes);
        if (!package.known) {
            if (unknown_count == std::numeric_limits<std::size_t>::max()) overflow = true;
            else ++unknown_count;
        }
        overflow |= !capacity_detail::Add(installed_bytes, package.installed_bytes);
        overflow |= !capacity_detail::Add(content_count, package.content_count);
    }
};

struct CapacityPlan {
    std::uint64_t reserve_bytes{};
    std::uint64_t required_bytes{};
    std::uint64_t remaining_bytes{};
    std::uint64_t shortage_bytes{};
    bool known{};
    bool fits{};
    bool calculated{};
    bool can_attempt{};
};

inline CapacityPlan EvaluateCapacity(StorageSpace space, const CapacityEstimate& estimate) {
    CapacityPlan plan;
    if (!capacity_detail::Valid(space) || estimate.overflow) return plan;

    const std::uint64_t base_reserve = estimate.installed_bytes || estimate.content_count || estimate.unknown_count ? 64 * 1024 * 1024 : 0;
    constexpr std::uint64_t per_content_reserve = 64 * 1024;
    if (estimate.content_count > (std::numeric_limits<std::uint64_t>::max() - base_reserve) / per_content_reserve)
        return plan;
    plan.reserve_bytes = base_reserve + estimate.content_count * per_content_reserve;
    plan.required_bytes = estimate.installed_bytes;
    if (!capacity_detail::Add(plan.required_bytes, plan.reserve_bytes)) return {};

    plan.calculated = true;
    plan.known = !estimate.unknown_count;
    plan.can_attempt = plan.required_bytes <= space.free_bytes;
    plan.fits = plan.known && plan.can_attempt;
    if (plan.can_attempt) plan.remaining_bytes = space.free_bytes - plan.required_bytes;
    else plan.shortage_bytes = plan.required_bytes - space.free_bytes;
    return plan;
}

struct CapacitySegments {
    double used{};
    double selected{};
    double reserve{};
    double free{};
};

inline CapacitySegments StorageBarSegments(StorageSpace space, const CapacityEstimate& estimate,
                                           const CapacityPlan& plan) {
    CapacitySegments segments;
    if (!capacity_detail::Valid(space)) return segments;
    const auto total = static_cast<double>(space.total_bytes);
    segments.used = static_cast<double>(space.total_bytes - space.free_bytes) / total;
    auto available = space.free_bytes;
    if (plan.calculated) {
        const auto selected = std::min(estimate.installed_bytes, available);
        available -= selected;
        const auto reserve = std::min(plan.reserve_bytes, available);
        available -= reserve;
        segments.selected = static_cast<double>(selected) / total;
        segments.reserve = static_cast<double>(reserve) / total;
    }
    segments.free = static_cast<double>(available) / total;
    return segments;
}

} // namespace nxloader
