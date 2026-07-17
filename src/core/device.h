// AppleBox — device base class (86Box device_t analogue). Devices attach to a
// machine, register scheduler events, respond to reset, and serialize state.
// SPDX-License-Identifier: MIT
#pragma once

#include <string>

#include "core/scheduler.h"
#include "core/types.h"

namespace ab {

class StateVisitor;

class Device {
public:
    explicit Device(std::string name) : name_(std::move(name)) {}
    virtual ~Device() = default;

    const std::string& name() const { return name_; }

    // Called once after all devices are constructed and wired.
    virtual void init(Scheduler& sched) { (void)sched; }
    virtual void reset() {}
    virtual void serialize(StateVisitor& v) { (void)v; }

private:
    std::string name_;
};

} // namespace ab
