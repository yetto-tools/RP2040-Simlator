// Unit tests for the RP2040 PWM (datasheet 4.5).
#include "doctest.h"

#include <cstdint>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "peripherals/gpio.h"
#include "peripherals/pwm.h"

using namespace rp2040;

namespace {

struct PwmFix {
    RegisterFile regs;
    Memory mem;
    Cpu cpu{regs, mem};
    Gpio gpio;
    Pwm pwm{cpu, gpio};

    PwmFix() { REQUIRE(pwm.attach(mem)); }

    std::uint32_t rd(std::uint32_t off) { return mem.read_word(Pwm::kBase + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(Pwm::kBase + off, v) == BusStatus::Ok);
    }
    // slice register: base + slice*0x14 + reg
    void sreg(unsigned slice, std::uint32_t reg, std::uint32_t v) {
        wr(slice * 0x14u + reg, v);
    }
};

}  // namespace

TEST_CASE_FIXTURE(PwmFix, "a free-running slice counts up to TOP and wraps") {
    sreg(0, 0x10, 3);          // TOP = 3
    sreg(0, 0x04, 0x010);      // DIV = 1.0
    sreg(0, 0x00, 1u);         // CSR.EN

    pwm.on_cycles(1); CHECK(pwm.counter(0) == 1);
    pwm.on_cycles(1); CHECK(pwm.counter(0) == 2);
    pwm.on_cycles(1); CHECK(pwm.counter(0) == 3);
    pwm.on_cycles(1); CHECK(pwm.counter(0) == 0);   // wrapped
    CHECK((rd(0xA4) & 1u) != 0);                    // INTR slice 0
}

TEST_CASE_FIXTURE(PwmFix, "the integer divider slows the counter") {
    sreg(1, 0x10, 0xFFFF);
    sreg(1, 0x04, 4u << 4);    // DIV = 4.0
    sreg(1, 0x00, 1u);

    pwm.on_cycles(3); CHECK(pwm.counter(1) == 0);
    pwm.on_cycles(1); CHECK(pwm.counter(1) == 1);   // 4 clocks -> 1 step
    pwm.on_cycles(4); CHECK(pwm.counter(1) == 2);
}

TEST_CASE_FIXTURE(PwmFix, "channel A drives its GPIO high while counter < CC.A") {
    gpio.set_funcsel(4, Gpio::kFuncPwm);   // slice 2, channel A -> GPIO4
    gpio.driver_set_pindir(Gpio::kPwm, 4, true);
    sreg(2, 0x10, 10);                     // TOP = 10
    sreg(2, 0x0C, 4u);                     // CC: A = 4
    sreg(2, 0x04, 0x010);
    sreg(2, 0x00, 1u);

    // counter 0..3 -> A high; 4..10 -> A low
    for (int i = 0; i < 4; ++i) { CHECK(gpio.level(4)); pwm.on_cycles(1); }
    for (int i = 4; i < 10; ++i) { CHECK_FALSE(gpio.level(4)); pwm.on_cycles(1); }
}

TEST_CASE_FIXTURE(PwmFix, "phase-correct mode counts up then down") {
    sreg(3, 0x10, 3);                      // TOP = 3
    sreg(3, 0x04, 0x010);
    sreg(3, 0x00, 1u | (1u << 1));         // CSR.EN | PH_CORRECT

    const int expect[] = {1, 2, 3, 2, 1, 0, 1, 2};
    for (int e : expect) { pwm.on_cycles(1); CHECK(pwm.counter(3) == static_cast<std::uint16_t>(e)); }
    CHECK((rd(0xA4) & (1u << 3)) != 0);    // wrap IRQ fired at the bottom
}

TEST_CASE_FIXTURE(PwmFix, "wrap interrupt is gated by INTE and reaches the NVIC") {
    sreg(0, 0x10, 1);
    sreg(0, 0x04, 0x010);
    wr(0xA8, 1u);                          // INTE slice 0
    sreg(0, 0x00, 1u);

    CHECK_FALSE(cpu.is_pending(Pwm::kIrqWrap));
    pwm.on_cycles(2);                      // 0 -> 1 -> wrap
    CHECK(cpu.is_pending(Pwm::kIrqWrap));
    wr(0xA4, 1u);                          // INTR w1c
    CHECK_FALSE(cpu.is_pending(Pwm::kIrqWrap));
}

TEST_CASE_FIXTURE(PwmFix, "atomic SET alias enables a slice without a read-modify-write") {
    sreg(6, 0x10, 5);
    sreg(6, 0x04, 0x010);
    wr(0x2000u + 0xA0u, 1u << 6);   // EN via the +0x2000 atomic-set alias
    pwm.on_cycles(1);
    CHECK(pwm.counter(6) == 1);
    wr(0x3000u + 0xA0u, 1u << 6);   // clear it again
    pwm.on_cycles(5);
    CHECK(pwm.counter(6) == 1);     // frozen
}

TEST_CASE_FIXTURE(PwmFix, "the global EN register also enables a slice") {
    sreg(5, 0x10, 2);
    sreg(5, 0x04, 0x010);
    wr(0xA0, 1u << 5);                     // EN: slice 5
    pwm.on_cycles(1);
    CHECK(pwm.counter(5) == 1);
}

TEST_CASE_FIXTURE(PwmFix, "DIVMODE=LEVEL gates the counter's clock on the B pin") {
    sreg(0, 0x10, 0xFFFF);
    sreg(0, 0x04, 0x010);           // DIV = 1.0 (would tick every cycle if ungated)
    gpio.set_external(1, false);    // slice 0 channel B = GPIO1, held low
    sreg(0, 0x00, 1u | (1u << 4));  // CSR.EN | DIVMODE=LEVEL

    pwm.on_cycles(10);
    CHECK(pwm.counter(0) == 0);     // B low: the clock is held, no progress at all

    gpio.set_external(1, true);
    pwm.on_cycles(5);
    CHECK(pwm.counter(0) == 5);     // B high: runs at the normal (DIV=1) rate
}

TEST_CASE_FIXTURE(PwmFix, "DIVMODE=RISE advances one count per B rising edge, bypassing DIV") {
    sreg(1, 0x10, 0xFFFF);
    sreg(1, 0x04, 0xF0u << 4);      // DIV = 240 - irrelevant in edge mode
    gpio.set_external(3, false);    // slice 1 channel B = GPIO3
    sreg(1, 0x00, 1u | (2u << 4));  // CSR.EN | DIVMODE=RISE

    pwm.on_cycles(50);
    CHECK(pwm.counter(1) == 0);     // no edges yet

    gpio.set_external(3, true);     // rising edge
    pwm.on_cycles(1);
    CHECK(pwm.counter(1) == 1);

    pwm.on_cycles(50);
    CHECK(pwm.counter(1) == 1);     // held high: no further edges

    gpio.set_external(3, false);
    pwm.on_cycles(1);
    CHECK(pwm.counter(1) == 1);     // falling edge: not counted in RISE mode

    gpio.set_external(3, true);
    pwm.on_cycles(1);
    CHECK(pwm.counter(1) == 2);     // second rising edge
}

TEST_CASE_FIXTURE(PwmFix, "DIVMODE=FALL advances one count per B falling edge") {
    sreg(2, 0x10, 0xFFFF);
    gpio.set_external(5, true);     // slice 2 channel B = GPIO5, starts high
    sreg(2, 0x00, 1u | (3u << 4));  // CSR.EN | DIVMODE=FALL

    pwm.on_cycles(1);
    CHECK(pwm.counter(2) == 0);     // no edge (still high, priming prev_b)

    gpio.set_external(5, false);
    pwm.on_cycles(1);
    CHECK(pwm.counter(2) == 1);     // falling edge

    gpio.set_external(5, true);
    pwm.on_cycles(1);
    CHECK(pwm.counter(2) == 1);     // rising edge: not counted in FALL mode
}

TEST_CASE_FIXTURE(PwmFix, "B's output driver is disabled while gated/edge DIVMODE is selected") {
    gpio.set_funcsel(1, Gpio::kFuncPwm);   // slice 0 channel B = GPIO1
    sreg(0, 0x00, 1u | (1u << 4));         // CSR.EN | DIVMODE=LEVEL
    CHECK_FALSE(gpio.pad_driving(1));      // B is an input now, not an output

    sreg(0, 0x00, 1u);                     // back to DIVMODE=FREE
    CHECK(gpio.pad_driving(1));            // B drives again
}

TEST_CASE_FIXTURE(PwmFix, "CSR.PH_ADV nudges the counter forward by exactly 1, self-clearing") {
    sreg(4, 0x10, 100);
    sreg(4, 0x04, 0x010);
    sreg(4, 0x00, 1u);              // CSR.EN, not yet advanced
    CHECK(pwm.counter(4) == 0);

    sreg(4, 0x00, 1u | (1u << 6));  // CSR.EN | PH_ADV
    CHECK(pwm.counter(4) == 1);     // applied immediately, no on_cycles() needed
    CHECK((rd(4 * 0x14u + 0x00u) & (1u << 6)) == 0);  // PH_ADV reads back clear
}

TEST_CASE_FIXTURE(PwmFix, "CSR.PH_RET nudges the counter backward by exactly 1, wrapping at 0") {
    sreg(7, 0x10, 100);
    sreg(7, 0x00, 1u);
    pwm.on_cycles(1);
    CHECK(pwm.counter(7) == 1);

    sreg(7, 0x00, 1u | (1u << 7));  // CSR.EN | PH_RET
    CHECK(pwm.counter(7) == 0);

    sreg(7, 0x00, 1u | (1u << 7));  // retard again, from 0 - wraps to TOP
    CHECK(pwm.counter(7) == 100);
    CHECK((rd(7 * 0x14u + 0x00u) & (1u << 7)) == 0);  // PH_RET reads back clear
}
