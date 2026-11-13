#include "nxloader/visible_range.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

struct Work {
    std::uint64_t visits{};
    std::uint64_t visible{};
    std::uint64_t checksum{};
};

__attribute__((noinline)) void ObserveCell(Work& work, std::int64_t index, double position) {
    ++work.visible;
    work.checksum += index + static_cast<std::uint64_t>(position);
}

__attribute__((noinline)) Work Legacy(std::int64_t count, float scroll) {
    Work work;
    float y = 143.f - scroll;
    for (std::int64_t i = 0; i < count; ++i, y += 60.f) {
        ++work.visits;
        if (y > 640.f) {
            break;
        }
        if (y + 60.f < 100.f) {
            continue;
        }
        ObserveCell(work, i, y);
    }
    return work;
}

__attribute__((noinline)) Work Bounded(std::int64_t count, float scroll) {
    Work work;
    work.visits = nxloader::VisitVisibleList(count, 1, 143. - scroll, 60., 60., 100., 640., [&](auto i, double y) {
        ObserveCell(work, i, y);
        return true;
    });
    return work;
}

struct Measurement {
    double milliseconds{};
    Work work;
};

template<class Traverse>
Measurement Measure(Traverse traverse, std::int64_t count, int frames) {
    const auto start = std::chrono::steady_clock::now();
    Work total;
    for (int frame = 0; frame < frames; ++frame) {
        const auto scroll = static_cast<float>((count - 12 - frame % 3) * 60);
        const auto work = traverse(count, scroll);
        total.visits += work.visits;
        total.visible += work.visible;
        total.checksum += work.checksum;
    }
    return {std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(), total};
}

int main(int argc, char** argv) {
    constexpr std::int64_t count = 100000;
    constexpr int frames = 1000;
    const int repetitions = argc == 2 && std::string_view(argv[1]) == "--profile" ? 50 : 5;
    std::vector<double> before, after;
    std::puts("Host-only row traversal microbenchmark: 100,000 items, 1,000 frames near the end; no GPU, filesystem, or install I/O.");
    for (int repetition = 0; repetition < repetitions; ++repetition) {
        const auto old = Measure(Legacy, count, frames);
        const auto now = Measure(Bounded, count, frames);
        if (old.work.visible != now.work.visible || old.work.checksum != now.work.checksum || now.work.visits != now.work.visible) {
            std::fputs("ERROR: work/output mismatch\n", stderr);
            return EXIT_FAILURE;
        }
        before.push_back(old.milliseconds);
        after.push_back(now.milliseconds);
        std::printf("run=%d old_ms=%.6f new_ms=%.6f old_visits=%llu new_visits=%llu visible=%llu checksum=%llu errors=0\n",
            repetition + 1, old.milliseconds, now.milliseconds,
            static_cast<unsigned long long>(old.work.visits), static_cast<unsigned long long>(now.work.visits),
            static_cast<unsigned long long>(now.work.visible), static_cast<unsigned long long>(now.work.checksum));
    }
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    std::printf("median_ms old=%.6f new=%.6f; range_ms old=[%.6f,%.6f] new=[%.6f,%.6f]\n",
        before[before.size() / 2], after[after.size() / 2], before.front(), before.back(), after.front(), after.back());
}
