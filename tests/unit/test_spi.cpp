// Unit tests for the RP2040 SPI (PL022 bit-accurate model, datasheet 4.4).
#include "doctest.h"

#include <cstdint>
#include <vector>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "peripherals/spi.h"

using namespace rp2040;

namespace {

struct SpiFix {
    RegisterFile regs;
    Memory mem;
    Cpu cpu{regs, mem};
    Spi spi{cpu, Spi::kSpi0Base, Spi::kSpi0Irq};

    SpiFix() {
        REQUIRE(spi.attach(mem));
        wr(0x00, 0x07);      // SSPCR0: DSS=8 bit (field 7), SCR=0
        wr(0x10, 2u);         // SSPCPSR=2 (minimum valid divisor)
        wr(0x04, 1u << 1);   // SSPCR1.SSE - enable
    }
    std::uint32_t rd(std::uint32_t off) { return mem.read_word(Spi::kSpi0Base + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(Spi::kSpi0Base + off, v) == BusStatus::Ok);
    }
    // One 8-bit frame = 8 bits * CPSDVSR(2) = 16 SSPCLK cycles; 30 gives margin.
    void advance_frames(int n) { spi.on_cycles(static_cast<std::uint64_t>(n) * 30u); }
};

}  // namespace

TEST_CASE_FIXTURE(SpiFix, "a DR write is a full-duplex transfer: MOSI logged, MISO fed back") {
    spi.feed(0xA5);
    spi.feed(0x3C);
    wr(0x08, 0x11);   // SSPDR
    wr(0x08, 0x22);
    advance_frames(2);

    const std::vector<std::uint8_t> out = spi.take_output();
    REQUIRE(out.size() == 2);
    CHECK(out[0] == 0x11);
    CHECK(out[1] == 0x22);

    CHECK((rd(0x0C) & (1u << 2)) != 0);   // SSPSR.RNE
    CHECK(rd(0x08) == 0xA5);              // RX pops the fed bytes in order
    CHECK(rd(0x08) == 0x3C);
    CHECK((rd(0x0C) & (1u << 2)) == 0);   // RX empty
}

TEST_CASE_FIXTURE(SpiFix, "a frame takes exactly one bit-clock period, not less") {
    wr(0x08, 0x11);
    spi.on_cycles(10);   // < 16 cycles needed for 8 bits @ 2 cyc/bit
    CHECK(spi.take_output().empty());
    spi.on_cycles(20);   // now past 30 total
    CHECK(spi.take_output() == std::vector<std::uint8_t>{0x11});
}

TEST_CASE_FIXTURE(SpiFix, "with no slave the RX byte is 0xFF (idle MISO)") {
    wr(0x08, 0x55);
    advance_frames(1);
    CHECK(rd(0x08) == 0xFF);
}

TEST_CASE_FIXTURE(SpiFix, "loopback (LBM) returns the transmitted byte") {
    wr(0x04, (1u << 1) | (1u << 0));   // SSE | LBM
    wr(0x08, 0x7E);
    advance_frames(1);
    CHECK(rd(0x08) == 0x7E);
}

TEST_CASE_FIXTURE(SpiFix, "the transfer callback drives MISO from MOSI") {
    spi.on_transfer([](std::uint8_t mosi) { return static_cast<std::uint8_t>(mosi ^ 0xFF); });
    wr(0x08, 0x0F);
    advance_frames(1);
    CHECK(rd(0x08) == 0xF0);
}

TEST_CASE_FIXTURE(SpiFix, "SSPSR shows TX empty / not full / not busy before any transfer") {
    CHECK((rd(0x0C) & (1u << 0)) != 0);   // TFE
    CHECK((rd(0x0C) & (1u << 1)) != 0);   // TNF
    CHECK((rd(0x0C) & (1u << 4)) == 0);   // BSY
}

TEST_CASE_FIXTURE(SpiFix, "SSPSR.BSY is set while a frame is in flight") {
    wr(0x08, 0x00);
    CHECK((rd(0x0C) & (1u << 4)) != 0);   // BSY: queued, not yet clocked out
    advance_frames(1);
    CHECK((rd(0x0C) & (1u << 4)) == 0);   // BSY clears once drained
}

TEST_CASE_FIXTURE(SpiFix, "RX interrupt is gated by SSPIMSC and reaches the NVIC") {
    spi.feed(0x01);
    wr(0x08, 0x00);                       // transfer -> RX now has a byte
    advance_frames(1);
    CHECK((rd(0x18) & (1u << 2)) != 0);   // SSPRIS.RXRIS
    CHECK_FALSE(cpu.is_pending(Spi::kSpi0Irq));

    wr(0x14, 1u << 2);                    // SSPIMSC.RXIM
    CHECK(cpu.is_pending(Spi::kSpi0Irq));
    CHECK(rd(0x08) == 0x01);              // drain -> deassert
}

TEST_CASE_FIXTURE(SpiFix, "the RX FIFO caps at 8 entries") {
    for (int i = 0; i < 12; ++i) {
        spi.feed(static_cast<std::uint8_t>(i));
        wr(0x08, 0);
        advance_frames(1);
    }
    CHECK((rd(0x0C) & (1u << 3)) != 0);   // SSPSR.RFF
    int drained = 0;
    while ((rd(0x0C) & (1u << 2)) != 0) { rd(0x08); ++drained; }
    CHECK(drained == 8);
}

TEST_CASE_FIXTURE(SpiFix, "a 16-bit frame (DSS) transfers the full width") {
    wr(0x00, 0x0F);                        // SSPCR0: DSS=16 bit (field 15)
    wr(0x08, 0xBEEF);
    wr(0x04, (1u << 1) | (1u << 0));       // SSE | LBM: loop it back
    spi.on_cycles(40);                      // 16 bits * 2 cyc/bit = 32 cycles needed
    CHECK(rd(0x08) == 0xBEEF);
}

TEST_CASE_FIXTURE(SpiFix, "with no bit-rate divisor configured (CPSDVSR=0), no data moves") {
    wr(0x10, 0u);                          // CPSDVSR=0: bit-rate generator off
    wr(0x08, 0x99);
    advance_frames(5);
    CHECK(spi.take_output().empty());
    CHECK((rd(0x0C) & (1u << 4)) != 0);    // BSY: still queued, never clocked out
}
