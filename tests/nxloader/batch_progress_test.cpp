#include "nxloader/batch_progress.hpp"

#include <cassert>
#include <iostream>

constexpr std::uint64_t second = 1'000'000'000;

int main() {
    nxloader::BatchProgress batch;
    assert(!batch.Started());

    // three games of 100, 50 and 50 bytes.
    batch.Start(0, 200);
    assert(batch.Started() && batch.Size() == 200);
    assert(batch.Offset(0) == 0);
    assert(!batch.SecondsRemaining(second / 2, 0));

    // the first game installs two content files, 80 and 20 bytes.
    batch.NextItem(100);
    assert(batch.Offset(40) == 40);
    batch.EndTransfer(80);
    assert(batch.Offset(0) == 80);
    assert(batch.Offset(10) == 90);
    // a game never counts for more than its size.
    assert(batch.Offset(500) == 100);
    batch.EndTransfer(20);

    // the second game fails part way; it still counts as done once the next starts.
    batch.NextItem(50);
    assert(batch.Offset(10) == 110);
    batch.EndTransfer(10);
    batch.NextItem(50);
    assert(batch.Offset(0) == 150);

    // 150 bytes in 10 seconds leaves 50 bytes at 15 a second.
    assert(batch.BytesPerSecond(10 * second, 0) == 15);
    assert(*batch.SecondsRemaining(10 * second, 0) == 4);

    batch.EndTransfer(50);
    assert(batch.Offset(0) == 200);
    assert(*batch.SecondsRemaining(11 * second, 0) == 0);

    // the total caps the list even if sizes were underestimated.
    batch.NextItem(30);
    assert(batch.Offset(30) == 200);

    // a new run starts clean.
    batch.Start(20 * second, 10);
    batch.NextItem(10);
    assert(batch.Offset(0) == 0);
    assert(batch.BytesPerSecond(20 * second + second / 2, 5) == 0);
    assert(batch.BytesPerSecond(22 * second, 5) == 2);

    // nothing to measure.
    batch.Start(0, 0);
    batch.NextItem(0);
    assert(!batch.SecondsRemaining(5 * second, 100));

    std::cout << "Batch progress tests passed.\n";
}
