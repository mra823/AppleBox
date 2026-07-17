// AppleBox — save-state round-trip tests.
// SPDX-License-Identifier: MIT
#include <array>
#include <cstdio>

#include "core/savestate.h"

using namespace ab;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

struct FakeDevice {
    u8 a = 0;
    u16 b = 0;
    s64 c = 0;
    std::array<u8, 16> mem{};

    void serialize(StateVisitor& v) {
        v.value("dev.a", a);
        v.value("dev.b", b);
        v.value("dev.c", c);
        v.array("dev.mem", std::span<u8>(mem));
    }
};

int main() {
    // Round trip through bytes.
    {
        FakeDevice src;
        src.a = 0xAB;
        src.b = 0x1234;
        src.c = -987654321;
        for (std::size_t i = 0; i < src.mem.size(); ++i) src.mem[i] = static_cast<u8>(i * 3);

        StateSnapshot snap;
        StateSnapshot::Writer w(snap);
        src.serialize(w);
        auto bytes = snap.toBytes();

        StateSnapshot loaded;
        CHECK(StateSnapshot::fromBytes(bytes, loaded));
        FakeDevice dst;
        StateSnapshot::Reader r(loaded);
        dst.serialize(r);
        CHECK(r.mismatches() == 0);
        CHECK(dst.a == src.a && dst.b == src.b && dst.c == src.c && dst.mem == src.mem);
    }

    // Corrupt/truncated data is rejected.
    {
        StateSnapshot snap;
        u32 x = 42;
        StateSnapshot::Writer w(snap);
        w.value("x", x);
        auto bytes = snap.toBytes();
        bytes.pop_back();
        StateSnapshot out;
        CHECK(!StateSnapshot::fromBytes(bytes, out));
        bytes.clear();
        CHECK(!StateSnapshot::fromBytes(bytes, out));
    }

    // Missing keys count as mismatches, fields keep defaults (version drift).
    {
        StateSnapshot empty;
        FakeDevice dst;
        dst.a = 7;
        StateSnapshot::Reader r(empty);
        dst.serialize(r);
        CHECK(r.mismatches() == 4);
        CHECK(dst.a == 7); // untouched
    }

    std::printf(failures ? "%d failure(s)\n" : "all savestate tests passed\n", failures);
    return failures;
}
