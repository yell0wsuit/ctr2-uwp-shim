#pragma once
namespace ctr2 {
// The game advances one fixed step per present and relies on Present's vsync
// to run at 60 Hz. Wine on macOS doesn't hold presents to the display (120 a
// second on a 60 Hz screen), and faster displays refresh more often; either way
// the game ran too fast. Returns how long to wait before presenting at `now` to
// keep presents `period` apart, and advances `next`, when the following present
// is due (0 before the first). Times are in any one unit.
inline long long PaceFrame(long long now, long long period, long long& next) {
    // On the first frame, or after falling more than a frame behind (a load or
    // a stall), start a new schedule instead of presenting a burst to catch up.
    if (next == 0 || now - next > period) next = now;
    const long long wait = next > now ? next - now : 0;
    next += period;
    return wait;
}
}
