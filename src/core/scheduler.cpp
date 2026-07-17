// SPDX-License-Identifier: MIT
#include "core/scheduler.h"

#include <cassert>

#include "core/savestate.h"

namespace ab {

Scheduler::EventId Scheduler::addEvent(std::string name, Callback cb) {
    events_.push_back(Event{std::move(name), std::move(cb), kNever});
    return static_cast<EventId>(events_.size() - 1);
}

void Scheduler::scheduleAt(EventId id, Ticks when) {
    assert(id < events_.size());
    events_[id].when = when;
}

void Scheduler::cancel(EventId id) {
    assert(id < events_.size());
    events_[id].when = kNever;
}

bool Scheduler::isScheduled(EventId id) const {
    assert(id < events_.size());
    return events_[id].when != kNever;
}

Ticks Scheduler::nextEventTime() const {
    Ticks best = kNever;
    for (const auto& e : events_)
        if (e.when < best) best = e.when;
    return best;
}

void Scheduler::runUntil(Ticks target) {
    for (;;) {
        // Find the earliest due event at or before target.
        std::size_t bestIdx = events_.size();
        Ticks best = kNever;
        for (std::size_t i = 0; i < events_.size(); ++i) {
            if (events_[i].when < best) {
                best = events_[i].when;
                bestIdx = i;
            }
        }
        if (bestIdx == events_.size() || best > target) break;
        Event& e = events_[bestIdx];
        e.when = kNever; // one-shot; callback may reschedule
        now_ = best;
        e.cb(now_);
    }
    if (now_ < target) now_ = target;
}

void Scheduler::serialize(StateVisitor& v) {
    v.value("scheduler.now", now_);
    // Event `when` values are serialized by name so ordering changes across
    // versions don't corrupt state.
    for (auto& e : events_) v.value("scheduler.event." + e.name, e.when);
}

} // namespace ab
