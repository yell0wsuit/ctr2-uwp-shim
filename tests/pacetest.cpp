// Present pacing at the 60 Hz the game's fixed-step update assumes.
#include "gpu/frame_pacing.h"
#include <cstdio>
using namespace ctr2;
static int failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
int main() {
    const long long period = 1200;
    // The first present goes out at once.
    long long next = 0;
    CHECK(PaceFrame(5000, period, next) == 0);
    CHECK(next == 5000 + period);
    // Presenting every half period (Wine on macOS, or a 120 Hz display): every present
    // waits for its slot, so they land exactly one period apart.
    next = 0;
    long long now = 10000;
    for (int frame = 0; frame < 20; ++frame) {
        const long long wait = PaceFrame(now, period, next);
        CHECK(now + wait == 10000 + frame * period);
        now += wait + period / 2;
    }
    // Presents already held to 60 Hz (vsync on Windows) never wait.
    next = 0;
    for (long long time = 0; time < 20 * period; time += period) CHECK(PaceFrame(1 + time, period, next) == 0);
    // A present that arrives early waits for the rest of its period.
    next = 0;
    PaceFrame(1000, period, next);
    CHECK(PaceFrame(1000 + period - 300, period, next) == 300);
    // Running slightly late keeps the schedule, so the next frame catches up.
    next = 0;
    PaceFrame(1000, period, next);
    CHECK(PaceFrame(1000 + period + 100, period, next) == 0);
    CHECK(next == 1000 + 2 * period);
    // A stall of more than a frame starts a new schedule instead of a burst.
    next = 0;
    PaceFrame(1000, period, next);
    CHECK(PaceFrame(1000 + 5 * period, period, next) == 0);
    CHECK(next == 1000 + 6 * period);
    CHECK(PaceFrame(1000 + 5 * period + 10, period, next) == period - 10);
    printf("pacetest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
