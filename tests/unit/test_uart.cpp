// Unit tests for the RP2040 UART (PL011 functional model, datasheet 4.2).
#include "doctest.h"

#include <cstdint>
#include <string>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "peripherals/uart.h"

using namespace rp2040;

namespace {

struct UartFix {
    RegisterFile regs;
    Memory mem;
    Cpu cpu{regs, mem};
    Uart uart{cpu, Uart::kUart0Base, Uart::kUart0Irq};

    UartFix() {
        REQUIRE(uart.attach(mem));
        wr(0x30, 1u | (1u << 8) | (1u << 9));   // UARTCR: UARTEN | TXE | RXE
    }
    std::uint32_t rd(std::uint32_t off) { return mem.read_word(Uart::kUart0Base + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(Uart::kUart0Base + off, v) == BusStatus::Ok);
    }
};

}  // namespace

TEST_CASE_FIXTURE(UartFix, "writing UARTDR transmits a byte to the output log") {
    wr(0x00, 'H');
    wr(0x00, 'i');
    CHECK(uart.take_output() == "Hi");
    CHECK((rd(0x18) & (1u << 7)) != 0);   // UARTFR.TXFE - always empty (instant TX)
}

TEST_CASE_FIXTURE(UartFix, "a fed byte is readable through UARTDR, FR tracks the FIFO") {
    CHECK((rd(0x18) & (1u << 4)) != 0);   // RXFE set - nothing received
    uart.feed("AB");
    CHECK((rd(0x18) & (1u << 4)) == 0);   // RXFE clear
    CHECK(rd(0x00) == 'A');
    CHECK(rd(0x00) == 'B');
    CHECK((rd(0x18) & (1u << 4)) != 0);   // empty again
    CHECK(rd(0x00) == 0u);                // read of empty FIFO -> 0
}

TEST_CASE_FIXTURE(UartFix, "RX interrupt is gated by IMSC and reaches the NVIC") {
    uart.feed('x');
    CHECK((rd(0x3C) & (1u << 4)) != 0);   // UARTRIS.RXRIS raw
    CHECK_FALSE(cpu.is_pending(Uart::kUart0Irq));  // masked

    wr(0x38, 1u << 4);                    // UARTIMSC.RXIM
    CHECK((rd(0x40) & (1u << 4)) != 0);   // UARTMIS
    CHECK(cpu.is_pending(Uart::kUart0Irq));

    CHECK(rd(0x00) == 'x');               // draining the FIFO deasserts RXRIS
    CHECK_FALSE(cpu.is_pending(Uart::kUart0Irq));
}

TEST_CASE_FIXTURE(UartFix, "the transmit callback sees each byte") {
    std::string seen;
    uart.on_transmit([&](std::uint8_t b) { seen.push_back(static_cast<char>(b)); });
    for (char c : std::string("pico")) wr(0x00, static_cast<std::uint32_t>(c));
    CHECK(seen == "pico");
}

TEST_CASE_FIXTURE(UartFix, "the RX FIFO caps at 32 bytes") {
    for (int i = 0; i < 40; ++i) uart.feed(static_cast<std::uint8_t>('0' + (i % 10)));
    CHECK((rd(0x18) & (1u << 6)) != 0);   // UARTFR.RXFF
    int drained = 0;
    while ((rd(0x18) & (1u << 4)) == 0) { rd(0x00); ++drained; }
    CHECK(drained == 32);
}
