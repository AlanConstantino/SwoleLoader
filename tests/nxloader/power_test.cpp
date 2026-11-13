#include "nxloader/battery.hpp"
#include "nxloader/placeholder_journal.hpp"

#include <cassert>
#include <iostream>

using nxloader::Battery;
using nxloader::BatteryAction;

int main() {
    // unknown readings never stop an install.
    assert(nxloader::CheckBattery({}) == BatteryAction::Continue);
    assert(!nxloader::BatteryNeedsWarning({}));

    assert(nxloader::BatteryNeedsWarning({29, false, true}));
    assert(!nxloader::BatteryNeedsWarning({30, false, true}));
    assert(!nxloader::BatteryNeedsWarning({5, true, true}));

    assert(nxloader::CheckBattery({16, false, true}) == BatteryAction::Continue);
    assert(nxloader::CheckBattery({15, false, true}) == BatteryAction::StopBeforeNext);
    assert(nxloader::CheckBattery({6, false, true}) == BatteryAction::StopBeforeNext);
    assert(nxloader::CheckBattery({5, false, true}) == BatteryAction::StopNow);
    assert(nxloader::CheckBattery({0, false, true}) == BatteryAction::StopNow);
    // on a full-power charger the battery doesn't matter.
    assert(nxloader::CheckBattery({1, true, true}) == BatteryAction::Continue);

    nxloader::PlaceholderEntry entry{5, {}};
    for (std::size_t i = 0; i < entry.id.size(); ++i) entry.id[i] = static_cast<std::uint8_t>(i * 17);
    const auto line = nxloader::FormatPlaceholderEntry(entry);
    assert(line == "5 00112233445566778899aabbccddeeff\n");
    const auto parsed = nxloader::ParsePlaceholderEntry(line);
    assert(parsed && parsed->storage_id == 5 && parsed->id == entry.id);
    assert(nxloader::ParsePlaceholderEntry("4 00112233445566778899AABBCCDDEEFF\r\n")->storage_id == 4);

    // damaged lines, as a power cut can leave a half-written one.
    assert(!nxloader::ParsePlaceholderEntry(""));
    assert(!nxloader::ParsePlaceholderEntry("5 0011223344"));
    assert(!nxloader::ParsePlaceholderEntry("5 00112233445566778899aabbccddeefg"));
    assert(!nxloader::ParsePlaceholderEntry(" 00112233445566778899aabbccddeeff"));
    assert(!nxloader::ParsePlaceholderEntry("256 00112233445566778899aabbccddeeff"));
    assert(!nxloader::ParsePlaceholderEntry("x 00112233445566778899aabbccddeeff"));

    std::cout << "Power tests passed: battery thresholds and the interrupted-install record.\n";
}
