// Unit tests for the RP2040 RTC (datasheet 4.8).
#include "doctest.h"

#include <cstdint>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "peripherals/rtc.h"

using namespace rp2040;

namespace {

// pack helpers matching the SETUP register layout
std::uint32_t setup0(unsigned y, unsigned mo, unsigned d) {
    return (y << 12) | (mo << 8) | d;
}
std::uint32_t setup1(unsigned dow, unsigned h, unsigned mi, unsigned s) {
    return (dow << 24) | (h << 16) | (mi << 8) | s;
}

struct RtcFix {
    RegisterFile regs;
    Memory mem;
    Cpu cpu{regs, mem};
    // Test pacing: RTC tick == system clock, CLKDIV_M1 = 0 -> 1 second / cycle.
    Rtc rtc{cpu, /*rtc_tick_hz=*/1, /*sys_clk_hz=*/1};

    RtcFix() {
        REQUIRE(rtc.attach(mem));
        wr(0x00, 0u);   // CLKDIV_M1 = 0 -> 1 RTC tick == 1 second (test pacing)
    }
    std::uint32_t rd(std::uint32_t off) { return mem.read_word(Rtc::kBase + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(Rtc::kBase + off, v) == BusStatus::Ok);
    }
    void load(std::uint32_t s0, std::uint32_t s1) {
        wr(0x04, s0);
        wr(0x08, s1);
        wr(0x0C, 1u | (1u << 4));         // CTRL: ENABLE | LOAD
    }
};

}  // namespace

TEST_CASE_FIXTURE(RtcFix, "the calendar advances one second at a time and rolls over") {
    load(setup0(2026, 8, 30), setup1(0, 23, 59, 58));

    CHECK((rd(0x1C) & 0x3Fu) == 58u);         // RTC_0 seconds
    rtc.on_cycles(3);                    // +3 s -> 00:00:01, day 31
    CHECK((rd(0x1C) & 0x3Fu) == 1u);          // sec
    CHECK(((rd(0x1C) >> 8) & 0x3Fu) == 0u);   // min
    CHECK(((rd(0x1C) >> 16) & 0x1Fu) == 0u);  // hour
    CHECK((rd(0x18) & 0x1Fu) == 31u);         // RTC_1 day
}

TEST_CASE_FIXTURE(RtcFix, "month/year roll over correctly") {
    load(setup0(2024, 2, 29), setup1(0, 23, 59, 59));   // leap year
    rtc.on_cycles(2);                    // 23:59:59 -> +2s -> 00:00:01, Mar 1
    CHECK((rd(0x18) & 0x1Fu) == 1u);          // day
    CHECK(((rd(0x18) >> 8) & 0xFu) == 3u);    // month
    CHECK(((rd(0x18) >> 12) & 0xFFFu) == 2024u);
}

TEST_CASE_FIXTURE(RtcFix, "a field-masked alarm raises RTC_IRQ") {
    load(setup0(2026, 1, 1), setup1(0, 12, 0, 55));
    wr(0x24, 1u);                             // INTE
    wr(0x14, (1u << 28) | 0u);                // IRQ_SETUP_1: MATCH_SEC, sec == 0
    CHECK_FALSE(cpu.is_pending(Rtc::kIrq));

    rtc.on_cycles(5);                    // 12:00:55 -> 12:01:00 : sec == 0 match
    CHECK((rd(0x20) & 1u) != 0);              // INTR
    CHECK(cpu.is_pending(Rtc::kIrq));

    wr(0x20, 1u);                             // INTR w1c
    CHECK_FALSE(cpu.is_pending(Rtc::kIrq));
}

TEST_CASE_FIXTURE(RtcFix, "a disabled RTC does not advance") {
    load(setup0(2026, 1, 1), setup1(0, 0, 0, 0));
    wr(0x0C, 0u);                             // disable
    rtc.on_cycles(100);
    CHECK((rd(0x1C) & 0x3Fu) == 0u);
}
