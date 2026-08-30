// Unit tests for the RP2040 SPI (PL022 functional model, datasheet 4.4).
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
        wr(0x04, 1u << 1);   // SSPCR1.SSE - enable
    }
    std::uint32_t rd(std::uint32_t off) { return mem.read_word(Spi::kSpi0Base + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(Spi::kSpi0Base + off, v) == BusStatus::Ok);
    }
};

}  // namespace

TEST_CASE_FIXTURE(SpiFix, "a DR write is a full-duplex transfer: MOSI logged, MISO fed back") {
    spi.feed(0xA5);
    spi.feed(0x3C);
    wr(0x08, 0x11);   // SSPDR
    wr(0x08, 0x22);

    const std::vector<std::uint8_t> out = spi.take_output();
    REQUIRE(out.size() == 2);
    CHECK(out[0] == 0x11);
    CHECK(out[1] == 0x22);

    CHECK((rd(0x0C) & (1u << 2)) != 0);   // SSPSR.RNE
    CHECK(rd(0x08) == 0xA5);              // RX pops the fed bytes in order
    CHECK(rd(0x08) == 0x3C);
    CHECK((rd(0x0C) & (1u << 2)) == 0);   // RX empty
}

TEST_CASE_FIXTURE(SpiFix, "with no slave the RX byte is 0xFF (idle MISO)") {
    wr(0x08, 0x55);
    CHECK(rd(0x08) == 0xFF);
}

TEST_CASE_FIXTURE(SpiFix, "loopback (LBM) returns the transmitted byte") {
    wr(0x04, (1u << 1) | (1u << 0));   // SSE | LBM
    wr(0x08, 0x7E);
    CHECK(rd(0x08) == 0x7E);
}

TEST_CASE_FIXTURE(SpiFix, "the transfer callback drives MISO from MOSI") {
    spi.on_transfer([](std::uint8_t mosi) { return static_cast<std::uint8_t>(mosi ^ 0xFF); });
    wr(0x08, 0x0F);
    CHECK(rd(0x08) == 0xF0);
}

TEST_CASE_FIXTURE(SpiFix, "SSPSR always shows TX empty / not full (instant TX)") {
    CHECK((rd(0x0C) & (1u << 0)) != 0);   // TFE
    CHECK((rd(0x0C) & (1u << 1)) != 0);   // TNF
}

TEST_CASE_FIXTURE(SpiFix, "RX interrupt is gated by SSPIMSC and reaches the NVIC") {
    spi.feed(0x01);
    wr(0x08, 0x00);                       // transfer -> RX now has a byte
    CHECK((rd(0x18) & (1u << 2)) != 0);   // SSPRIS.RXRIS
    CHECK_FALSE(cpu.is_pending(Spi::kSpi0Irq));

    wr(0x14, 1u << 2);                    // SSPIMSC.RXIM
    CHECK(cpu.is_pending(Spi::kSpi0Irq));
    CHECK(rd(0x08) == 0x01);              // drain -> deassert
    // TX interrupt is still asserted (always ready), so unmask only RX above.
}

TEST_CASE_FIXTURE(SpiFix, "the RX FIFO caps at 8 entries") {
    for (int i = 0; i < 12; ++i) { spi.feed(static_cast<std::uint8_t>(i)); wr(0x08, 0); }
    CHECK((rd(0x0C) & (1u << 3)) != 0);   // SSPSR.RFF
    int drained = 0;
    while ((rd(0x0C) & (1u << 2)) != 0) { rd(0x08); ++drained; }
    CHECK(drained == 8);
}
