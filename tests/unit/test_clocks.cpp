// Unit tests for the minimal clock-tree peripherals (XOSC / PLL / CLOCKS).
// These exist so pico-sdk's clocks_init() runs without spinning.
#include "doctest.h"

#include <cstdint>

#include "core/memory.h"
#include "peripherals/clocks.h"

using namespace rp2040;

TEST_CASE("XOSC reports STABLE once CTRL.ENABLE has the magic value") {
    Memory mem;
    Xosc xosc;
    REQUIRE(xosc.attach(mem));

    CHECK((mem.read_word(Xosc::kBase + 0x04).value & (1u << 31)) == 0);   // not stable
    mem.write_word(Xosc::kBase + 0x00, 0xFAB000u | 0xAA0u);               // ENABLE=0xFAB, FREQ_RANGE
    CHECK((mem.read_word(Xosc::kBase + 0x04).value & (1u << 31)) != 0);   // STATUS.STABLE
    CHECK((mem.read_word(Xosc::kBase + 0x04).value & (1u << 12)) != 0);   // STATUS.ENABLED
}

TEST_CASE("PLL reports LOCK when powered and not bypassed") {
    Memory mem;
    Pll pll(Pll::kSysBase);
    REQUIRE(pll.attach(mem));

    // Reset PWR = all bits set (powered down) -> no lock.
    CHECK((mem.read_word(Pll::kSysBase + 0x00).value & (1u << 31)) == 0);
    mem.write_word(Pll::kSysBase + 0x08, 125u);          // FBDIV_INT
    mem.write_word(Pll::kSysBase + 0x04, 0u);            // PWR: power everything up
    CHECK((mem.read_word(Pll::kSysBase + 0x00).value & (1u << 31)) != 0);  // CS.LOCK

    mem.write_word(Pll::kSysBase + 0x00, 1u << 8);       // CS.BYPASS
    CHECK((mem.read_word(Pll::kSysBase + 0x00).value & (1u << 31)) == 0);  // lock drops
}

TEST_CASE("PLL derives the pico-sdk 125 MHz clk_sys from a 12 MHz reference") {
    Memory mem;
    Pll pll(Pll::kSysBase);
    REQUIRE(pll.attach(mem));

    // pico-sdk default: FBDIV=125, POSTDIV1=6, POSTDIV2=2 -> 12M*125/12 = 125M.
    mem.write_word(Pll::kSysBase + 0x08, 125u);                       // FBDIV_INT
    mem.write_word(Pll::kSysBase + 0x0C, (6u << 16) | (2u << 12));    // PRIM
    mem.write_word(Pll::kSysBase + 0x04, 0u);                        // PWR: powered

    CHECK(pll.feedback_divider() == 125u);
    CHECK(pll.postdiv1() == 6u);
    CHECK(pll.postdiv2() == 2u);
    CHECK(pll.output_hz(12'000'000u) == 125'000'000u);

    mem.write_word(Pll::kSysBase + 0x00, 1u << 8);                    // BYPASS
    CHECK(pll.output_hz(12'000'000u) == 0u);                         // not locked
}

TEST_CASE("CLOCKS: SELECTED follows CTRL.SRC and DIV round-trips") {
    Memory mem;
    Clocks clk;
    REQUIRE(clk.attach(mem));

    // clk_ref (generator 0): CTRL @ +0x00, DIV @ +0x04, SELECTED @ +0x08.
    CHECK(mem.read_word(Clocks::kBase + 0x08).value == 0x1u);   // SRC 0 -> bit 0
    mem.write_word(Clocks::kBase + 0x00, 0x2u);                 // CTRL.SRC = 2
    CHECK(mem.read_word(Clocks::kBase + 0x08).value == (1u << 2));

    mem.write_word(Clocks::kBase + 0x04, 0x00030000u);          // DIV
    CHECK(mem.read_word(Clocks::kBase + 0x04).value == 0x00030000u);
}

TEST_CASE("ROSC boots enabled + stable and clears STABLE when disabled") {
    Memory mem;
    Rosc rosc;
    REQUIRE(rosc.attach(mem));

    const std::uint32_t status = Rosc::kBase + 0x18;
    std::uint32_t s = mem.read_word(status).value;
    CHECK((s & (1u << 31)) != 0);   // STABLE out of reset
    CHECK((s & (1u << 12)) != 0);   // ENABLED
    CHECK((s & (1u << 16)) != 0);   // DIV_RUNNING

    mem.write_word(Rosc::kBase + 0x00, 0xD1Eu << 12);   // CTRL.ENABLE = DISABLE
    s = mem.read_word(status).value;
    CHECK((s & (1u << 31)) == 0);
    CHECK((s & (1u << 12)) == 0);
}

TEST_CASE("ROSC password-guards FREQA/FREQB and latches BADWRITE (w1c)") {
    Memory mem;
    Rosc rosc;
    REQUIRE(rosc.attach(mem));
    const std::uint32_t status = Rosc::kBase + 0x18;

    mem.write_word(Rosc::kBase + 0x04, 0x00001111u);            // no password
    CHECK((mem.read_word(status).value & (1u << 24)) != 0);     // BADWRITE
    CHECK(mem.read_word(Rosc::kBase + 0x04).value == 0u);       // rejected

    mem.write_word(status, 1u << 24);                           // write-1-clear
    CHECK((mem.read_word(status).value & (1u << 24)) == 0);

    mem.write_word(Rosc::kBase + 0x04, 0x96960055u);            // correct password
    CHECK(mem.read_word(Rosc::kBase + 0x04).value == 0x0055u);
    CHECK((mem.read_word(status).value & (1u << 24)) == 0);
}

TEST_CASE("ROSC RANDOMBIT toggles and DIV round-trips") {
    Memory mem;
    Rosc rosc;
    REQUIRE(rosc.attach(mem));
    const std::uint32_t a = mem.read_word(Rosc::kBase + 0x1C).value & 1u;
    const std::uint32_t b = mem.read_word(Rosc::kBase + 0x1C).value & 1u;
    CHECK(a != b);
    mem.write_word(Rosc::kBase + 0x10, 0xAA0u + 8u);
    CHECK(mem.read_word(Rosc::kBase + 0x10).value == 0xAA8u);
}

TEST_CASE("clock-tree peripherals honour the atomic SET alias") {
    Memory mem;
    Clocks clk;
    REQUIRE(clk.attach(mem));
    // clk_sys (generator 1): CTRL @ +0x0C. Atomic-set the ENABLE bit (11).
    mem.write_word(Clocks::kBase + 0x2000u + 0x0Cu, 1u << 11);
    CHECK((mem.read_word(Clocks::kBase + 0x0Cu).value & (1u << 11)) != 0);
}
