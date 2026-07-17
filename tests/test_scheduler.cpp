// AppleBox — scheduler unit tests (no framework; exit code = failures).
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <vector>

#include "core/scheduler.h"

using namespace ab;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

int main() {
    // Empty scheduler advances time.
    {
        Scheduler s;
        s.runUntil(12345);
        CHECK(s.now() == 12345);
        CHECK(s.nextEventTime() == Scheduler::kNever);
    }

    // Events fire in time order, exactly once, at the right time.
    {
        Scheduler s;
        std::vector<int> order;
        auto a = s.addEvent("a", [&](Ticks t) { order.push_back(1); CHECK(t == 100); });
        auto b = s.addEvent("b", [&](Ticks t) { order.push_back(2); CHECK(t == 50); });
        s.scheduleAt(a, 100);
        s.scheduleAt(b, 50);
        s.runUntil(200);
        CHECK(order.size() == 2);
        CHECK(order[0] == 2 && order[1] == 1);
        CHECK(!s.isScheduled(a) && !s.isScheduled(b));
        CHECK(s.now() == 200);
    }

    // Events beyond the target do not fire.
    {
        Scheduler s;
        int fired = 0;
        auto e = s.addEvent("late", [&](Ticks) { ++fired; });
        s.scheduleAt(e, 1000);
        s.runUntil(999);
        CHECK(fired == 0);
        CHECK(s.isScheduled(e));
        s.runUntil(1000);
        CHECK(fired == 1);
    }

    // Callbacks may reschedule themselves (periodic device tick).
    {
        Scheduler s;
        int ticks = 0;
        Scheduler::EventId e{};
        e = s.addEvent("periodic", [&](Ticks t) {
            ++ticks;
            if (ticks < 5) s.scheduleAt(e, t + 10);
        });
        s.scheduleAt(e, 10);
        s.runUntil(100);
        CHECK(ticks == 5);
    }

    // Cancel prevents firing.
    {
        Scheduler s;
        int fired = 0;
        auto e = s.addEvent("x", [&](Ticks) { ++fired; });
        s.scheduleAt(e, 10);
        s.cancel(e);
        s.runUntil(20);
        CHECK(fired == 0);
    }

    // ClockRatio: 1 MHz 6502 vs a 14.318 MHz master clock example.
    {
        ClockRatio r{14, 1}; // 14 master ticks per CPU cycle
        CHECK(r.toTicks(1'000'000) == 14'000'000);
        CHECK(r.toCycles(14'000'000) == 1'000'000);
    }

    std::printf(failures ? "%d failure(s)\n" : "all scheduler tests passed\n", failures);
    return failures;
}
