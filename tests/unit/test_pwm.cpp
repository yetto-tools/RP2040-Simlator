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
