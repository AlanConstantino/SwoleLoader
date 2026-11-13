#include "nxloader/transfer_rate.hpp"

#include <cassert>
#include <iostream>
#include <limits>

constexpr std::uint64_t second = 1'000'000'000;

int main() {
    nxloader::TransferRate rate;
    rate.Reset(0);
    rate.Update(second / 2, 1'000'000);
    assert(rate.BytesPerSecond() == 0);
    rate.Update(second * 3 / 2, 3'000'000);
    assert(rate.BytesPerSecond() == 2'000'000);
    rate.Update(second * 4, 8'000'000);
    assert(rate.BytesPerSecond() == 2'000'000);

    // A hidden window or delayed sample must not report all intervening bytes as one second.
    rate.Update(second * 14, 28'000'000);
    assert(rate.BytesPerSecond() == 2'000'000);
    rate.Update(second * 15, 28'000'000);
    assert(rate.BytesPerSecond() == 0);
    assert(!nxloader::TransferSecondsRemaining(30'000'000, 28'000'000, rate.BytesPerSecond()));

    rate.Update(second * 16, 30'000'000);
    assert(rate.BytesPerSecond() == 2'000'000);
    rate.Reset(second * 16);
    assert(rate.BytesPerSecond() == 0);
    rate.Update(second * 17, 500'000);
    assert(rate.BytesPerSecond() == 500'000);

    rate.Update(second * 17 + second / 2, 750'000);
    rate.Update(second * 17 + second * 3 / 4, 600'000);
    assert(rate.BytesPerSecond() == 0);
    rate.Update(second * 18 + second * 3 / 4, 1'600'000);
    assert(rate.BytesPerSecond() == 1'000'000);

    rate.Update(second, 1'600'000);
    assert(rate.BytesPerSecond() == 0);
    rate.Update(second * 2, -1);
    assert(rate.BytesPerSecond() == 0);
    rate.Update(second * 3, 1'000'000);
    assert(rate.BytesPerSecond() == 1'000'000);

    assert(nxloader::TransferSecondsRemaining(1001, 0, 1000) == 2);
    assert(nxloader::TransferSecondsRemaining(1000, 1000, 0) == 0);
    assert(nxloader::TransferSecondsRemaining(1000, 1001, 1000) == 0);
    assert(!nxloader::TransferSecondsRemaining(0, 0, 1000));
    assert(!nxloader::TransferSecondsRemaining(1000, -1, 1000));

    rate.Reset(0);
    rate.Update(second, std::numeric_limits<std::int64_t>::max());
    assert(rate.BytesPerSecond() > 0);
    assert(nxloader::TransferSecondsRemaining(std::numeric_limits<std::int64_t>::max(),
        0, rate.BytesPerSecond()) == 1);
    std::cout << "Transfer rate tests passed: elapsed intervals, idle, resets, rewinds, and ETA.\n";
}
