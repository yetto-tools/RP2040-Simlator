// Unit tests for RESETS_WDSEL (datasheet 2.14): a watchdog-triggered reset
// also resets whichever peripherals RESETS_WDSEL selects, on top of the
// core(s) (see PSM_WDSEL, already covered by test_watchdog.cpp). Exercises
// a representative slice of the ~13 peripheral classes wired into
// Simulator's watchdog_.on_peripheral_reset() callback, including the
// trickiest one (PIO, whose reset() must reach into PioBlock and every SM).
#include "doctest.h"

#include <cstdint>

#include "peripherals/dma.h"
#include "peripherals/pwm.h"
#include "peripherals/resets.h"
#include "peripherals/uart.h"
#include "peripherals/watchdog.h"
#include "pio/pio_registers.h"
#include "simulator.h"

using namespace rp2040;

namespace {

// RESETS_RESET / RESETS_WDSEL bit assignments (datasheet 2.14.1).
constexpr std::uint32_t kWdselDma = 1u << 2;
constexpr std::uint32_t kWdselPio0 = 1u << 10;
constexpr std::uint32_t kWdselPwm = 1u << 14;
constexpr std::uint32_t kWdselUart0 = 1u << 22;
constexpr std::uint32_t kWdselUart1 = 1u << 23;

struct SimFix {
    Simulator sim;
    std::uint32_t rd(std::uint32_t addr) { return sim.memory().read_word(addr).value; }
    void wr(std::uint32_t addr, std::uint32_t v) {
        REQUIRE(sim.memory().write_word(addr, v) == BusStatus::Ok);
    }
    void set_wdsel(std::uint32_t mask) { wr(Resets::kBase + 0x04u, mask); }
    void trigger_watchdog() { wr(Watchdog::kBase + 0x00u, 1u << 31); }  // CTRL.TRIGGER
};

}  // namespace

TEST_CASE_FIXTURE(SimFix, "RESETS_WDSEL selects UART0 for reset, UART1 unaffected") {
    wr(Uart::kUart0Base + 0x24u, 0x1234u);  // UART0 IBRD
    wr(Uart::kUart0Base + 0x30u, 0xABCu);   // UART0 CR
    wr(Uart::kUart1Base + 0x24u, 0x5678u);  // UART1 IBRD (a different instance)

    set_wdsel(kWdselUart0);
    trigger_watchdog();

    CHECK(rd(Uart::kUart0Base + 0x24u) == 0u);      // IBRD back to POR (0)
    CHECK(rd(Uart::kUart0Base + 0x30u) == 0x300u);  // CR back to POR (TXE|RXE, UARTEN clear)
    CHECK(rd(Uart::kUart1Base + 0x24u) == 0x5678u); // UART1's own IBRD is untouched
}

TEST_CASE_FIXTURE(SimFix, "RESETS_WDSEL with neither UART bit set leaves both alone") {
    wr(Uart::kUart0Base + 0x24u, 0x1234u);
    wr(Uart::kUart1Base + 0x24u, 0x5678u);

    set_wdsel(kWdselUart1);   // selects UART1 only - not UART0
    trigger_watchdog();

    CHECK(rd(Uart::kUart0Base + 0x24u) == 0x1234u);  // untouched
    CHECK(rd(Uart::kUart1Base + 0x24u) == 0u);       // reset
}

TEST_CASE_FIXTURE(SimFix, "RESETS_WDSEL selects PWM for reset") {
    wr(Pwm::kBase + 0x00u, 0x1u);        // slice 0 CSR.EN
    wr(Pwm::kBase + 0x10u, 0x8000u);     // slice 0 TOP
    wr(Pwm::kBase + 0xA0u, 0xFFu);       // global EN, all slices

    set_wdsel(kWdselPwm);
    trigger_watchdog();

    CHECK(rd(Pwm::kBase + 0x00u) == 0u);
    CHECK(rd(Pwm::kBase + 0x10u) == 0xFFFFu);  // TOP back to its POR default (not 0)
    CHECK(rd(Pwm::kBase + 0xA0u) == 0u);
}

TEST_CASE_FIXTURE(SimFix, "RESETS_WDSEL selects DMA for reset") {
    wr(Dma::kBase + 0x00u, 0x20000000u);  // channel 0 READ_ADDR
    wr(Dma::kBase + 0x04u, 0x20001000u);  // channel 0 WRITE_ADDR

    set_wdsel(kWdselDma);
    trigger_watchdog();

    CHECK(rd(Dma::kBase + 0x00u) == 0u);
    CHECK(rd(Dma::kBase + 0x04u) == 0u);
}

TEST_CASE_FIXTURE(SimFix, "RESETS_WDSEL selects PIO0 for reset (program memory and SM config)") {
    wr(PioRegisters::kPio0Base + 0x048u, 0xBEEFu);  // INSTR_MEM0
    wr(PioRegisters::kPio0Base + 0x000u, 0x1u);     // CTRL: SM0_ENABLE
    wr(PioRegisters::kPio0Base + 0x0C8u, 0x00020000u);  // SM0_CLKDIV: int=2

    set_wdsel(kWdselPio0);
    trigger_watchdog();

    CHECK(rd(PioRegisters::kPio0Base + 0x048u) == 0u);   // instruction memory cleared
    CHECK(rd(PioRegisters::kPio0Base + 0x000u) == 0u);   // CTRL (SM disabled again)
    CHECK(rd(PioRegisters::kPio0Base + 0x0C8u) == 0u);   // CLKDIV shadow cleared
}

TEST_CASE_FIXTURE(SimFix, "a non-watchdog reset does not touch RESETS_WDSEL-selected peripherals") {
    wr(Uart::kUart0Base + 0x24u, 0x1234u);
    set_wdsel(kWdselUart0);
    sim.reset();  // plain CPU reset (e.g. Simulator::load()) - not the watchdog
    CHECK(rd(Uart::kUart0Base + 0x24u) == 0x1234u);
}
