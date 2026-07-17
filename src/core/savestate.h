// AppleBox — versioned save-state visitor. Devices implement serialize() and
// visit each field by name; the same code path saves and loads. No pointers
// or coroutine state may be serialized (MAME state-saving constraint).
// SPDX-License-Identifier: MIT
#pragma once

#include <cstring>
#include <map>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "core/types.h"

namespace ab {

class StateVisitor {
public:
    virtual ~StateVisitor() = default;
    virtual bool isLoading() const = 0;

    template <typename T>
        requires std::is_arithmetic_v<T> || std::is_enum_v<T>
    void value(const std::string& key, T& v) {
        raw(key, &v, sizeof(T));
    }

    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void array(const std::string& key, std::span<T> data) {
        raw(key, data.data(), data.size_bytes());
    }

protected:
    virtual void raw(const std::string& key, void* data, std::size_t size) = 0;
};

// Snapshot container: a key → bytes map with a format version header.
class StateSnapshot {
public:
    static constexpr u32 kFormatVersion = 1;

    // --- writing ---
    class Writer;
    // --- reading ---
    class Reader;

    std::vector<u8> toBytes() const;
    static bool fromBytes(std::span<const u8> bytes, StateSnapshot& out);

    std::map<std::string, std::vector<u8>>& entries() { return entries_; }
    const std::map<std::string, std::vector<u8>>& entries() const { return entries_; }

private:
    std::map<std::string, std::vector<u8>> entries_;
};

class StateSnapshot::Writer final : public StateVisitor {
public:
    explicit Writer(StateSnapshot& snap) : snap_(snap) {}
    bool isLoading() const override { return false; }

protected:
    void raw(const std::string& key, void* data, std::size_t size) override {
        auto& e = snap_.entries()[key];
        e.resize(size);
        std::memcpy(e.data(), data, size);
    }

private:
    StateSnapshot& snap_;
};

class StateSnapshot::Reader final : public StateVisitor {
public:
    explicit Reader(const StateSnapshot& snap) : snap_(snap) {}
    bool isLoading() const override { return true; }

    // Keys present in the snapshot but never visited, or visited with a size
    // mismatch, are counted so callers can detect version drift.
    int mismatches() const { return mismatches_; }

protected:
    void raw(const std::string& key, void* data, std::size_t size) override {
        auto it = snap_.entries().find(key);
        if (it == snap_.entries().end() || it->second.size() != size) {
            ++mismatches_; // leave the field at its reset default
            return;
        }
        std::memcpy(data, it->second.data(), size);
    }

private:
    const StateSnapshot& snap_;
    int mismatches_ = 0;
};

} // namespace ab
