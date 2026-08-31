// Unit tests for ClockTree: resolving the CLOCKS generators into real Hz.
#include "doctest.h"

#include <array>
#include <cstdint>

#include "core/memory.h"
#include "peripherals/clock_tree.h"
#include "peripherals/clocks.h"
#include "peripherals/timer.h"
#include "peripherals/watchdog.h"
#include "simulator.h"

using namespace rp2040;

namespace {
struct Fix {
    Memory mem;
    Xosc xosc;
    Rosc rosc;
    Pll pll_sys{Pll::kSysBase};
    Pll pll_usb{Pll::kUsbBase};
    Clocks clocks;
    Watchdog wd;
    ClockTree tree{xosc, rosc, pll_sys, pll_usb, clocks, wd};

    Fix() {
        REQUIRE(xosc.attach(mem));
        REQUIRE(rosc.attach(mem));
        REQUIRE(pll_sys.attach(mem));
        REQUIRE(pll_usb.attach(mem));
        REQUIRE(clocks.attach(mem));
        REQUIRE(wd.attach(mem));
    }
    void w(std::uint32_t addr, std::uint32_t v) {
        REQUIRE(mem.write_word(addr, v) == BusStatus::Ok);
    }
    void lock_pll(std::uint32_t base, unsigned fbdiv, unsigned pd1, unsigned pd2) {
        w(base + 0x08, fbdiv);                       // FBDIV_INT
        w(base + 0x0C, (pd1 << 16) | (pd2 << 12));   // PRIM
        w(base + 0x04, 0);                           // PWR: powered
    }
};
}  // namespace

TEST_CASE_FIXTURE(Fix, "unconfigured tree returns the pico-sdk steady-state defaults") {
    CHECK(tree.clk_sys_hz() == 125'000'000u);
    CHECK(tree.clk_peri_hz() == 125'000'000u);
    CHECK(tree.clk_adc_hz() == 48'000'000u);
    CHECK(tree.clk_rtc_hz() == 46'875u);
    CHECK(tree.timer_us_cycles() == 125u);   // tick 12 * 125MHz / 12MHz
}

TEST_CASE_FIXTURE(Fix, "clk_sys follows PLL_SYS and the CLK_SYS divider") {
    lock_pll(Pll::kSysBase, 125, 6, 2);              // 12M*125/12 = 125 MHz
    REQUIRE(pll_sys.output_hz(12'000'000u) == 125'000'000u);

    w(Clocks::kBase + 0x3C, 0x1u);                   // CLK_SYS_CTRL: SRC = AUX (PLL_SYS)
    w(Clocks::kBase + 0x40, 0x100u);                 // CLK_SYS_DIV: int 1
    CHECK(tree.clk_sys_hz() == 125'000'000u);

    w(Clocks::kBase + 0x40, 0x200u);                 // int 2
    CHECK(tree.clk_sys_hz() == 62'500'000u);

    w(Clocks::kBase + 0x40, 0x180u);                 // int 1, frac 0x80 -> /1.5
    CHECK(tree.clk_sys_hz() == 83'333'333u);
}

TEST_CASE_FIXTURE(Fix, "clk_adc / clk_rtc follow PLL_USB") {
    lock_pll(Pll::kUsbBase, 40, 5, 2);               // 12M*40/10 = 48 MHz

    w(Clocks::kBase + 0x60, 0x0u);                   // CLK_ADC_CTRL: AUXSRC 0 = PLL_USB
    w(Clocks::kBase + 0x64, 0x100u);
    CHECK(tree.clk_adc_hz() == 48'000'000u);

    w(Clocks::kBase + 0x6C, 0x0u);                   // CLK_RTC_CTRL: AUXSRC 0 = PLL_USB
    w(Clocks::kBase + 0x70, 1024u << 8);             // /1024
    CHECK(tree.clk_rtc_hz() == 46'875u);
}

TEST_CASE_FIXTURE(Fix, "the microsecond tick scales with WATCHDOG_TICK and clk_sys") {
    lock_pll(Pll::kSysBase, 100, 5, 2);              // 12M*100/10 = 120 MHz
    w(Clocks::kBase + 0x3C, 0x1u);
    w(Clocks::kBase + 0x40, 0x100u);
    REQUIRE(tree.clk_sys_hz() == 120'000'000u);

    w(Watchdog::kBase + 0x2C, 12u);                  // WATCHDOG_TICK.CYCLES = 12
    CHECK(tree.timer_us_cycles() == 120u);           // 12 * 120MHz / 12MHz

    w(Watchdog::kBase + 0x2C, 6u);                   // half the clk_ref divisor
    CHECK(tree.timer_us_cycles() == 60u);
}

TEST_CASE_FIXTURE(Fix, "signature changes only when a clock register is touched") {
    const std::uint32_t s0 = tree.signature();
    CHECK(tree.signature() == s0);                   // stable
    w(Clocks::kBase + 0x3C, 0x1u);
    CHECK(tree.signature() != s0);
}

TEST_CASE("Simulator paces the TIMER from the configured clock tree") {
    Simulator sim;
    Memory& mem = sim.memory();

    // A tiny thread that just spins; step() advances the peripherals.
    const std::array<std::uint16_t, 1> prog{0xE7FE};
    REQUIRE(mem.load(0x20000000u, prog.data(), 2));
    sim.regs(0).set_pc(0x20000000u);

    // Default (unconfigured) clk_sys is 125 MHz.
    CHECK(sim.clk_sys_hz() == 125'000'000u);

    // Halve clk_sys: PLL_SYS 12M*100/10 = 120 MHz then CLK_SYS_DIV = 2 -> 60 MHz.
    REQUIRE(mem.write_word(Pll::kSysBase + 0x08, 100u) == BusStatus::Ok);
    REQUIRE(mem.write_word(Pll::kSysBase + 0x0C, (5u << 16) | (2u << 12)) == BusStatus::Ok);
    REQUIRE(mem.write_word(Pll::kSysBase + 0x04, 0u) == BusStatus::Ok);
    REQUIRE(mem.write_word(Clocks::kBase + 0x3C, 0x1u) == BusStatus::Ok);
    REQUIRE(mem.write_word(Clocks::kBase + 0x40, 0x200u) == BusStatus::Ok);
    REQUIRE(mem.write_word(Watchdog::kBase + 0x2Cu, 12u) == BusStatus::Ok);  // TICK.CYCLES

    for (int i = 0; i < 5; ++i) sim.step();          // let sync_clock_pacing() run
    CHECK(sim.clk_sys_hz() == 60'000'000u);

    // At 60 MHz the us tick is 12 * 60M/12M = 60 clk_sys cycles. Run ~6000
    // cycles' worth of spinning and the timer should read ~100 us.
    const std::uint64_t start_us = sim.timer().now_us();
    for (int i = 0; i < 6000; ++i) sim.step();       // b . costs ~3 cycles each
    const std::uint64_t elapsed = sim.timer().now_us() - start_us;
    CHECK(elapsed >= 250u);
    CHECK(elapsed <= 320u);                          // ~6000*3 / 60 = 300 us
}
