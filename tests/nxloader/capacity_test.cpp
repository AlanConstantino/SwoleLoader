#include "nxloader/capacity.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

constexpr std::uint64_t KiB = 1024;
constexpr std::uint64_t MiB = 1024 * KiB;
constexpr std::uint64_t GiB = 1024 * MiB;

void fractions_are_bounded(nxloader::StorageSpace space, const nxloader::CapacityEstimate& estimate,
                           const nxloader::CapacityPlan& plan) {
    const auto segments = nxloader::StorageBarSegments(space, estimate, plan);
    for (const auto fraction : {segments.used, segments.selected, segments.reserve, segments.free}) {
        assert(std::isfinite(fraction) && fraction >= 0 && fraction <= 1);
    }
    assert(std::abs(segments.used + segments.selected + segments.reserve + segments.free - 1) < 1e-12);
}

int main() {
    nxloader::CapacityEstimate estimate;
    estimate.Add({2 * GiB, 5 * GiB, 12, true, {}});
    estimate.Add({GiB, 3 * GiB, 4, true, {}});
    assert(estimate.source_bytes == 3 * GiB);
    assert(estimate.installed_bytes == 8 * GiB);
    assert(estimate.content_count == 16 && !estimate.overflow && !estimate.unknown_count);

    const nxloader::StorageSpace storage{10 * GiB, 32 * GiB, true};
    const auto plan = nxloader::EvaluateCapacity(storage, estimate);
    assert(plan.known && plan.fits);
    assert(plan.reserve_bytes == 64 * MiB + 16 * 64 * KiB);
    assert(plan.required_bytes == 8 * GiB + plan.reserve_bytes);
    assert(plan.remaining_bytes == storage.free_bytes - plan.required_bytes);
    assert(!plan.shortage_bytes);
    fractions_are_bounded(storage, estimate, plan);

    const nxloader::StorageSpace exact{plan.required_bytes, 32 * GiB, true};
    const auto exact_plan = nxloader::EvaluateCapacity(exact, estimate);
    assert(exact_plan.known && exact_plan.fits && !exact_plan.remaining_bytes);
    fractions_are_bounded(exact, estimate, exact_plan);

    const nxloader::StorageSpace one_short{plan.required_bytes - 1, 32 * GiB, true};
    const auto short_plan = nxloader::EvaluateCapacity(one_short, estimate);
    assert(short_plan.known && !short_plan.fits && short_plan.shortage_bytes == 1);
    assert(!short_plan.remaining_bytes);
    fractions_are_bounded(one_short, estimate, short_plan);

    const nxloader::StorageSpace very_short{GiB, 32 * GiB, true};
    const auto huge_plan = nxloader::EvaluateCapacity(very_short, estimate);
    fractions_are_bounded(very_short, estimate, huge_plan);
    assert(nxloader::StorageBarSegments(very_short, estimate, huge_plan).reserve == 0);

    for (const auto bad : {nxloader::StorageSpace{}, nxloader::StorageSpace{1, 0, true},
                           nxloader::StorageSpace{5, 4, true}, nxloader::StorageSpace{GiB, 2 * GiB, false}}) {
        const auto unknown = nxloader::EvaluateCapacity(bad, estimate);
        assert(!unknown.known && !unknown.fits);
        const auto bar = nxloader::StorageBarSegments(bad, estimate, unknown);
        assert(bar.used == 0 && bar.selected == 0 && bar.reserve == 0 && bar.free == 0);
    }

    auto unknown = estimate;
    unknown.Add({MiB, GiB, 9, false, "Partly readable metadata"});
    assert(unknown.source_bytes == estimate.source_bytes + MiB);
    assert(unknown.installed_bytes == estimate.installed_bytes + GiB && unknown.content_count == estimate.content_count + 9);
    assert(unknown.unknown_count == 1);
    const auto unknown_plan = nxloader::EvaluateCapacity(storage, unknown);
    assert(!unknown_plan.known && !unknown_plan.fits);
    assert(unknown_plan.calculated && unknown_plan.can_attempt);
    const auto unknown_bar = nxloader::StorageBarSegments(storage, unknown, unknown_plan);
    assert(unknown_bar.selected > 0 && unknown_bar.reserve > 0);
    assert(unknown_plan.remaining_bytes < plan.remaining_bytes);
    assert(!nxloader::EvaluateCapacity(very_short, unknown).can_attempt);
    nxloader::CapacityEstimate only_unknown;
    only_unknown.Add({MiB, 0, 0, false, "unsupported compression metadata"});
    assert(nxloader::EvaluateCapacity(storage, only_unknown).can_attempt);
    fractions_are_bounded(storage, unknown, unknown_plan);

    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    nxloader::CapacityEstimate overflow;
    overflow.Add({max, 0, 1, true, {}});
    overflow.Add({1, 0, 1, true, {}});
    assert(overflow.overflow && !nxloader::EvaluateCapacity(storage, overflow).fits);
    overflow = {};
    overflow.Add({0, max, 1, true, {}});
    overflow.Add({0, 1, 1, true, {}});
    assert(overflow.overflow && !nxloader::EvaluateCapacity(storage, overflow).known);
    overflow = {};
    overflow.Add({0, 1, max, true, {}});
    assert(!overflow.overflow && !nxloader::EvaluateCapacity({max, max, true}, overflow).known);
    overflow.Add({0, 1, 1, true, {}});
    assert(overflow.overflow);

    nxloader::CapacityEstimate required_overflow;
    required_overflow.installed_bytes = max - 10;
    required_overflow.content_count = 1;
    assert(!nxloader::EvaluateCapacity({max, max, true}, required_overflow).known);

    nxloader::CapacityEstimate count_overflow;
    count_overflow.unknown_count = std::numeric_limits<std::size_t>::max();
    count_overflow.Add({});
    assert(count_overflow.overflow);
    std::cout << "Capacity tests passed: reserve, shortages, unknown sizes, overflow, and storage-bar bounds.\n";
}
