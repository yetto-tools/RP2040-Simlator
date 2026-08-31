// Unit tests for the RP2040 ADC (datasheet 4.9).
#include "doctest.h"

#include <cstdint>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "peripherals/adc.h"

using namespace rp2040;

namespace {

struct AdcFix {
    RegisterFile regs;
    Memory mem;
    Cpu cpu{regs, mem};
    Adc adc{cpu};

    AdcFix() { REQUIRE(adc.attach(mem)); }

    std::uint32_t rd(std::uint32_t off) { return mem.read_word(Adc::kBase + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(Adc::kBase + off, v) == BusStatus::Ok);
    }
    // 96 ADC clocks at 48 MHz == 2 us == ~250 system clocks (default 125 MHz);
    // 260 gives comfortable margin.
    void advance() { adc.on_cycles(260); }
};

constexpr std::uint32_t kEN = 1u << 0;
constexpr std::uint32_t kTS_EN = 1u << 1;
constexpr std::uint32_t kSTART_ONCE = 1u << 2;
constexpr std::uint32_t kSTART_MANY = 1u << 3;
constexpr std::uint32_t kREADY = 1u << 8;

}  // namespace

TEST_CASE_FIXTURE(AdcFix, "START_ONCE converts the selected channel into RESULT") {
    adc.set_input(0, 0x123);
    adc.set_input(2, 0x7AB);

    wr(0x00, kEN | (2u << 12) | kSTART_ONCE);   // CS: EN, AINSEL=2, START_ONCE
    CHECK((rd(0x00) & kREADY) == 0);             // busy for the whole conversion
    advance();
    CHECK(rd(0x04) == 0x7AB);                    // RESULT
    CHECK((rd(0x00) & kREADY) != 0);

    wr(0x00, kEN | (0u << 12) | kSTART_ONCE);
    advance();
    CHECK(rd(0x04) == 0x123);
}

TEST_CASE_FIXTURE(AdcFix, "a conversion takes exactly 96 + DIV_INT ADC clocks, not less") {
    adc.set_input(0, 0x2AA);
    wr(0x00, kEN | kSTART_ONCE);
    adc.on_cycles(200);   // < ~250 cycles needed for 96 ADC clocks @ 48/125 MHz
    CHECK((rd(0x00) & kREADY) == 0);
    adc.on_cycles(60);    // now past ~260 total
    CHECK((rd(0x00) & kREADY) != 0);
    CHECK(rd(0x04) == 0x2AA);
}

TEST_CASE_FIXTURE(AdcFix, "START_ONCE is ignored while a conversion is already in flight") {
    adc.set_input(0, 0x111);
    wr(0x00, kEN | kSTART_ONCE);   // begin converting
    wr(0x00, kEN | kSTART_ONCE);   // re-issued mid-conversion: has no effect
    advance();
    CHECK(rd(0x04) == 0x111);      // exactly one conversion happened
    CHECK((rd(0x00) & kREADY) != 0);
}

TEST_CASE_FIXTURE(AdcFix, "the temperature-sensor channel needs TS_EN") {
    adc.set_input(Adc::kTempChannel, 0x333);
    wr(0x00, kEN | (4u << 12) | kSTART_ONCE);          // TS_EN off
    advance();
    CHECK(rd(0x04) == 0);
    wr(0x00, kEN | kTS_EN | (4u << 12) | kSTART_ONCE); // TS_EN on
    advance();
    CHECK(rd(0x04) == 0x333);
}

TEST_CASE_FIXTURE(AdcFix, "conversions land in the FIFO when FCS.EN is set") {
    adc.set_input(1, 0x2C0);
    wr(0x08, 1u);                                // FCS.EN
    wr(0x00, kEN | (1u << 12) | kSTART_ONCE);
    advance();
    wr(0x00, kEN | (1u << 12) | kSTART_ONCE);
    advance();

    CHECK(((rd(0x08) >> 16) & 0xFu) == 2u);      // FCS.LEVEL
    CHECK((rd(0x08) & (1u << 8)) == 0);          // not EMPTY
    CHECK(rd(0x0C) == 0x2C0);                    // FIFO pop
    CHECK(rd(0x0C) == 0x2C0);
    CHECK((rd(0x08) & (1u << 8)) != 0);          // EMPTY again
}

TEST_CASE_FIXTURE(AdcFix, "FCS.SHIFT stores an 8-bit sample") {
    adc.set_input(0, 0x0FF0);
    wr(0x08, 1u | (1u << 1));                    // FCS.EN | FCS.SHIFT
    wr(0x00, kEN | kSTART_ONCE);
    advance();
    CHECK(rd(0x0C) == 0xFF);                     // (0x0FF0 >> 4) & 0xFF
}

TEST_CASE_FIXTURE(AdcFix, "the FIFO overflows past 4 entries and sets FCS.OVER") {
    adc.set_input(0, 0x100);
    wr(0x08, 1u);
    for (int i = 0; i < 6; ++i) { wr(0x00, kEN | kSTART_ONCE); advance(); }
    CHECK(((rd(0x08) >> 16) & 0xFu) == 4u);      // capped
    CHECK((rd(0x08) & (1u << 10)) != 0);         // FCS.OVER
    wr(0x08, 1u | (1u << 10));                   // write-1-clear OVER
    CHECK((rd(0x08) & (1u << 10)) == 0);
}

TEST_CASE_FIXTURE(AdcFix, "free-running mode paces samples off the ADC clock") {
    adc.set_input(0, 0x055);
    wr(0x08, 1u);
    wr(0x10, 0u);                                // DIV = 0 -> 96 ADC clocks / sample
    wr(0x00, kEN | kSTART_MANY);

    // 96 ADC clocks at 48 MHz == 2 us == 250 system clocks.
    adc.on_cycles(200);
    CHECK(adc.fifo_level() == 0);
    adc.on_cycles(120);                          // now past the first sample
    CHECK(adc.fifo_level() >= 1);
}

TEST_CASE_FIXTURE(AdcFix, "FIFO interrupt is gated by INTE and reaches the NVIC") {
    adc.set_input(0, 0x0A0);
    wr(0x08, 1u | (1u << 24));                   // FCS.EN, THRESH=1
    wr(0x18, 1u);                                // INTE
    CHECK_FALSE(cpu.is_pending(Adc::kIrq));

    wr(0x00, kEN | kSTART_ONCE);                 // one sample -> level 1 >= thresh
    advance();
    CHECK(cpu.is_pending(Adc::kIrq));
    CHECK(rd(0x0C) == 0x0A0);                    // drain
    CHECK_FALSE(cpu.is_pending(Adc::kIrq));
}
