// Unit tests for the RP2040 UART (PL011 bit-accurate model, datasheet 4.2).
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
        wr(0x2C, (3u << 5));                     // LCR_H: WLEN=8 (11), no parity, 1 stop
        wr(0x24, 1u);                             // IBRD=1
        wr(0x28, 0u);                             // FBRD=0 -> bit period = 16 UARTCLK cycles
        wr(0x30, 1u | (1u << 8) | (1u << 9));     // UARTCR: UARTEN | TXE | RXE
    }
    std::uint32_t rd(std::uint32_t off) { return mem.read_word(Uart::kUart0Base + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(Uart::kUart0Base + off, v) == BusStatus::Ok);
    }
    // A frame is 10 bits (start + 8 data + stop) * 16 UARTCLK cycles/bit =
    // 160 cycles; 200 gives comfortable margin per byte.
    void advance_bytes(int n) { uart.on_cycles(static_cast<std::uint64_t>(n) * 200u); }
};

}  // namespace

TEST_CASE_FIXTURE(UartFix, "writing UARTDR transmits a byte after one frame period") {
    wr(0x00, 'H');
    CHECK((rd(0x18) & (1u << 3)) != 0);    // UARTFR.BUSY - queued, not yet shifted out
    CHECK(uart.take_output().empty());
    advance_bytes(1);
    CHECK(uart.take_output() == "H");
    CHECK((rd(0x18) & (1u << 7)) != 0);    // UARTFR.TXFE once drained
    CHECK((rd(0x18) & (1u << 3)) == 0);    // BUSY clears
}

TEST_CASE_FIXTURE(UartFix, "TX takes exactly one frame period, not less") {
    wr(0x00, 'Z');
    uart.on_cycles(150);                   // < 160 cycles needed for 10 bits @ 16 cyc/bit
    CHECK(uart.take_output().empty());
    uart.on_cycles(50);                    // now past 200 total
    CHECK(uart.take_output() == "Z");
}

TEST_CASE_FIXTURE(UartFix, "a fed byte is readable through UARTDR after one frame period") {
    CHECK((rd(0x18) & (1u << 4)) != 0);    // RXFE set - nothing received
    uart.feed("AB");
    CHECK((rd(0x18) & (1u << 4)) != 0);    // still nothing - on the wire, not arrived yet
    advance_bytes(2);
    CHECK((rd(0x18) & (1u << 4)) == 0);    // RXFE clear
    CHECK(rd(0x00) == 'A');
    CHECK(rd(0x00) == 'B');
    CHECK((rd(0x18) & (1u << 4)) != 0);    // empty again
    CHECK(rd(0x00) == 0u);                 // read of empty FIFO -> 0
}

TEST_CASE_FIXTURE(UartFix, "RX interrupt is gated by IMSC and reaches the NVIC") {
    uart.feed('x');
    advance_bytes(1);
    CHECK((rd(0x3C) & (1u << 4)) != 0);    // UARTRIS.RXRIS raw
    CHECK_FALSE(cpu.is_pending(Uart::kUart0Irq));  // masked

    wr(0x38, 1u << 4);                     // UARTIMSC.RXIM
    CHECK((rd(0x40) & (1u << 4)) != 0);    // UARTMIS
    CHECK(cpu.is_pending(Uart::kUart0Irq));

    CHECK(rd(0x00) == 'x');                // draining the FIFO deasserts RXRIS
    CHECK_FALSE(cpu.is_pending(Uart::kUart0Irq));
}

TEST_CASE_FIXTURE(UartFix, "the transmit callback sees each byte, in order") {
    std::string seen;
    uart.on_transmit([&](std::uint8_t b) { seen.push_back(static_cast<char>(b)); });
    for (char c : std::string("pico")) wr(0x00, static_cast<std::uint32_t>(c));
    advance_bytes(4);
    CHECK(seen == "pico");
}

TEST_CASE_FIXTURE(UartFix, "the RX FIFO caps at 32 bytes, further arrivals set OE") {
    for (int i = 0; i < 40; ++i) uart.feed(static_cast<std::uint8_t>('0' + (i % 10)));
    advance_bytes(40);
    CHECK((rd(0x18) & (1u << 6)) != 0);    // UARTFR.RXFF
    CHECK((rd(0x3C) & (1u << 10)) != 0);   // UARTRIS.OERIS - the extra 8 bytes were dropped
    int drained = 0;
    while ((rd(0x18) & (1u << 4)) == 0) { rd(0x00); ++drained; }
    CHECK(drained == 32);
}

TEST_CASE_FIXTURE(UartFix, "a framing error tagged on feed() surfaces in DR, RSR and RIS") {
    uart.feed('E', Uart::kRxErrFraming);
    advance_bytes(1);
    CHECK((rd(0x3C) & (1u << 7)) != 0);    // UARTRIS.FERIS
    const std::uint32_t dr = rd(0x00);
    CHECK((dr & 0xFFu) == 'E');
    CHECK((dr & (1u << 8)) != 0);          // DR.FE
    CHECK((rd(0x04) & 1u) != 0);           // UARTRSR.FE mirrors the last DR read
    wr(0x04, 0u);                          // ECR: any write clears the error latch
    CHECK(rd(0x04) == 0u);
    CHECK((rd(0x3C) & (1u << 7)) == 0);    // FERIS cleared too
}

TEST_CASE_FIXTURE(UartFix, "a parity error tagged on feed() sets PERIS") {
    uart.feed('E', Uart::kRxErrParity);
    advance_bytes(1);
    CHECK((rd(0x3C) & (1u << 8)) != 0);    // UARTRIS.PERIS
    CHECK((rd(0x00) & (1u << 9)) != 0);    // DR.PE
}

TEST_CASE_FIXTURE(UartFix, "a break tagged on feed() sets BERIS") {
    uart.feed('E', Uart::kRxErrBreak);
    advance_bytes(1);
    CHECK((rd(0x3C) & (1u << 9)) != 0);    // UARTRIS.BERIS
    CHECK((rd(0x00) & (1u << 10)) != 0);   // DR.BE
}

TEST_CASE_FIXTURE(UartFix, "LCR_H.BRK holds off new transmissions until cleared") {
    wr(0x2C, rd(0x2C) | 1u);               // set BRK
    wr(0x00, 'X');
    advance_bytes(2);
    CHECK(uart.take_output().empty());     // held: nothing sent while BRK is set
    wr(0x2C, rd(0x2C) & ~1u);              // clear BRK
    advance_bytes(1);
    CHECK(uart.take_output() == "X");
}

TEST_CASE_FIXTURE(UartFix, "with no baud rate configured (IBRD=0), no data moves") {
    wr(0x24, 0u);                          // IBRD=0: baud generator disabled
    wr(0x00, 'N');
    uart.feed('M');
    advance_bytes(5);
    CHECK(uart.take_output().empty());
    CHECK((rd(0x18) & (1u << 4)) != 0);    // RXFE still set - nothing arrived
}
