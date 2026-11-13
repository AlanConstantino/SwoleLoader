#pragma once

#include <cstdint>

namespace nxloader {

struct Battery {
    std::uint32_t percent{};
    // on a charger that keeps up with the console. a weak charger can still
    // lose charge, so it counts as not charging.
    bool charging{};
    bool known{};
};

// Below this, starting a batch on battery asks first.
constexpr std::uint32_t BatteryWarnPercent = 30;
// At or below this no new game is started, so a game is never cut off by the
// console shutting down.
constexpr std::uint32_t BatteryStopPercent = 15;
// At or below this the game being installed is cancelled, which removes its
// partly written data while there is still power to do so.
constexpr std::uint32_t BatteryCriticalPercent = 5;

enum class BatteryAction { Continue, StopBeforeNext, StopNow };

inline bool BatteryNeedsWarning(const Battery& battery) {
    return battery.known && !battery.charging && battery.percent < BatteryWarnPercent;
}

inline BatteryAction CheckBattery(const Battery& battery) {
    if (!battery.known || battery.charging) return BatteryAction::Continue;
    if (battery.percent <= BatteryCriticalPercent) return BatteryAction::StopNow;
    if (battery.percent <= BatteryStopPercent) return BatteryAction::StopBeforeNext;
    return BatteryAction::Continue;
}

} // namespace nxloader
