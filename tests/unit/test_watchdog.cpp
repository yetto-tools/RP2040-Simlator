// Unit tests for the RP2040 Watchdog + RESETS (datasheet 4.7, 2.14).
#include "doctest.h"

#include <cstdint>

#include "core/memory.h"
#include "peripherals/resets.h"
#include "peripherals/watchdog.h"

using namespace rp2040;

namespace {

struct WdFix {
    Memory mem;
    Watchdog wd{/*cycles_per_us=*/10};
    int resets = 0;

    WdFix() {
        REQUIRE(wd.attach(mem));
        wd.on_reset([&] { ++resets; });
    }
    std::uint32_t rd(std::uint32_t off) { return mem.read_word(Watchdog::kBase + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(Watchdog::kBase + off, v) == BusStatus::Ok);
    }
};

}  // namespace

TEST_CASE_FIXTURE(WdFix, "an un-fed watchdog counts down and resets") {
    wr(0x04, 100u);                 // LOAD = 100
    wr(0x00, 1u << 30);             // CTRL.ENABLE

    wd.on_cycles(10 * 20);          // 20 us -> -40 (2 per us)
    CHECK(resets == 0);
    CHECK((rd(0x00) & 0x00FFFFFFu) == 60u);

    wd.on_cycles(10 * 40);          // 40 more us -> hits 0
    CHECK(resets == 1);
    CHECK((rd(0x08) & 1u) != 0);    // REASON.TIMER
}

TEST_CASE_FIXTURE(WdFix, "feeding the watchdog (LOAD) keeps it alive") {
    wr(0x04, 50u);
    wr(0x00, 1u << 30);
    for (int i = 0; i < 10; ++i) {
        wd.on_cycles(10 * 20);      // 20 us: would drop 40
        wr(0x04, 50u);              // ...but we feed it back to 50
    }
    CHECK(resets == 0);
}

TEST_CASE_FIXTURE(WdFix, "CTRL.TRIGGER forces an immediate reset") {
    wr(0x00, 1u << 31);
    CHECK(resets == 1);
    CHECK((rd(0x08) & (1u << 1)) != 0);   // REASON.FORCE
}

TEST_CASE_FIXTURE(WdFix, "SCRATCH registers persist across a reset") {
    wr(0x0C, 0xCAFEBABEu);          // SCRATCH0
    wr(0x28, 0x12345678u);          // SCRATCH7
    wr(0x00, 1u << 31);             // trigger reset
    CHECK(rd(0x0C) == 0xCAFEBABEu);
    CHECK(rd(0x28) == 0x12345678u);
    CHECK(wd.scratch(0) == 0xCAFEBABEu);
}

TEST_CASE("RESETS: RESET_DONE mirrors ~RESET so unreset_block_wait returns") {
    Memory mem;
    Resets resets;
    REQUIRE(resets.attach(mem));

    // Reset value: everything held.
    CHECK(mem.read_word(Resets::kBase + 0x08).value == 0u);   // RESET_DONE

    // unreset_block(PIO0 | UART0): clear those RESET bits via the atomic clear alias.
    const std::uint32_t bits = (1u << 10) | (1u << 22);
    REQUIRE(mem.write_word(Resets::kBase + 0x3000 + 0x00, bits) == BusStatus::Ok);
    CHECK((mem.read_word(Resets::kBase + 0x00).value & bits) == 0u);       // RESET
    CHECK((mem.read_word(Resets::kBase + 0x08).value & bits) == bits);     // RESET_DONE
}
