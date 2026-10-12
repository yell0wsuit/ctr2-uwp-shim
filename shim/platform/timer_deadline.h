#pragma once
#include <chrono>
#include <cstdint>
#include <limits>
#include <ratio>
namespace ctr2 {
using TimerClock = std::chrono::steady_clock;
inline TimerClock::time_point TimerDeadline(TimerClock::time_point now, std::int64_t ticks) noexcept {
    // Both supported Windows standard libraries use signed 64-bit nanoseconds
    // for steady_clock. Check before either scaling 100 ns ticks or adding them.
    static_assert(std::ratio_equal<TimerClock::period, std::nano>::value,
                  "Timer deadline conversion requires a nanosecond clock");
    static_assert(std::numeric_limits<TimerClock::rep>::is_signed && sizeof(TimerClock::rep) == 8,
                  "Timer deadline conversion requires signed 64-bit clock ticks");
    if (ticks <= 0) return now;
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (ticks > maximum / 100) return TimerClock::time_point::max();
    auto nanoseconds = ticks * 100;
    if (now.time_since_epoch().count() > maximum - nanoseconds) return TimerClock::time_point::max();
    return now + std::chrono::nanoseconds(nanoseconds);
}
}
