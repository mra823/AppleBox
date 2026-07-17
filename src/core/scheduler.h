// AppleBox — master-clock scheduler. Single 64-bit tick timeline; device
// clocks are rational ratios of the master clock. The CPU runs in slices up
// to the next scheduled event.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "core/types.h"

namespace ab {

class StateVisitor;

// Converts a device clock to master ticks: ticks = cycles * num / den.
struct ClockRatio {
    s64 num = 1;
    s64 den = 1;

    constexpr Ticks toTicks(s64 deviceCycles) const {
        return deviceCycles * num / den;
    }
    constexpr s64 toCycles(Ticks ticks) const { return ticks * den / num; }
};

class Scheduler {
public:
    using EventId = u32;
    using Callback = std::function<void(Ticks now)>;
    static constexpr Ticks kNever = std::numeric_limits<Ticks>::max();

    Ticks now() const { return now_; }

    // Register a named event; returns a stable id. Events start unscheduled.
    EventId addEvent(std::string name, Callback cb);

    // Schedule `id` to fire at absolute tick `when` (replaces prior schedule).
    void scheduleAt(EventId id, Ticks when);
    void scheduleIn(EventId id, Ticks delta) { scheduleAt(id, now_ + delta); }
    void cancel(EventId id);
    bool isScheduled(EventId id) const;

    // Earliest pending event time, or kNever.
    Ticks nextEventTime() const;

    // Advance the timeline to `target`, firing due events in time order.
    void runUntil(Ticks target);

    void serialize(StateVisitor& v);

private:
    struct Event {
        std::string name;
        Callback cb;
        Ticks when = kNever; // kNever = unscheduled
    };
    std::vector<Event> events_;
    Ticks now_ = 0;
};

} // namespace ab
