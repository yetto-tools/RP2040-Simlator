// Unit tests for the ILI9341 SPI TFT virtual device (ili9341.h) - a thin
// St7789 (st7789.h) specialization with a different default panel
// resolution. test_st7789.cpp already covers the shared command decoder in
// depth (BGR swap, sub-window wrap, SWRESET, CS gating); these tests only
// check what Ili9341 actually changes: the 240x320 resolution itself.
#include "doctest.h"

#include <cstdint>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "peripherals/gpio.h"
#include "peripherals/ili9341.h"
#include "peripherals/spi.h"

using namespace rp2040;

namespace {

constexpr unsigned kCsPin = 5;
constexpr unsigned kDcPin = 6;

struct Ili9341Fix {
    RegisterFile regs;
    Memory mem;
    Cpu cpu{regs, mem};
    Spi spi{cpu, Spi::kSpi0Base, Spi::kSpi0Irq};
    Gpio gpio;
    Ili9341 tft{gpio, kCsPin, kDcPin};

    Ili9341Fix() {
        REQUIRE(spi.attach(mem));
        wr(0x00, 0x07);
        wr(0x10, 2u);
        wr(0x04, 1u << 1);
        spi.on_transfer([this](std::uint8_t b) { return tft.on_transfer(b); });
        select();
    }

    std::uint32_t rd(std::uint32_t off) { return mem.read_word(Spi::kSpi0Base + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(Spi::kSpi0Base + off, v) == BusStatus::Ok);
    }
    void select() { gpio.set_external(kCsPin, false); }
    void dc_command() { gpio.set_external(kDcPin, false); }
    void dc_data() { gpio.set_external(kDcPin, true); }

    void send(std::uint8_t byte) {
        wr(0x08, byte);
        spi.on_cycles(30);
    }
    void command(std::uint8_t cmd) {
        dc_command();
        send(cmd);
    }
    void data(std::uint8_t byte) {
        dc_data();
        send(byte);
    }
    void set_window(std::uint16_t x0, std::uint16_t x1, std::uint16_t y0, std::uint16_t y1) {
        command(0x2A);
        data(static_cast<std::uint8_t>(x0 >> 8));
        data(static_cast<std::uint8_t>(x0));
        data(static_cast<std::uint8_t>(x1 >> 8));
        data(static_cast<std::uint8_t>(x1));
        command(0x2B);
        data(static_cast<std::uint8_t>(y0 >> 8));
        data(static_cast<std::uint8_t>(y0));
        data(static_cast<std::uint8_t>(y1 >> 8));
        data(static_cast<std::uint8_t>(y1));
    }
    void write_pixel(std::uint16_t rgb565) {
        data(static_cast<std::uint8_t>(rgb565 >> 8));
        data(static_cast<std::uint8_t>(rgb565));
    }
};

}  // namespace

TEST_CASE_FIXTURE(Ili9341Fix, "the framebuffer defaults to the full 240x320 panel") {
    CHECK(tft.framebuffer().size() == static_cast<std::size_t>(Ili9341::kWidth) * Ili9341::kHeight);
}

TEST_CASE_FIXTURE(Ili9341Fix, "a full-height column write reaches row 319, beyond ST7789's 240 rows") {
    set_window(0, 0, 0, Ili9341::kHeight - 1);
    command(0x2C);  // RAMWR
    for (unsigned i = 0; i < Ili9341::kHeight; ++i) write_pixel(0x1234);

    const auto& fb = tft.framebuffer();
    CHECK(fb[(Ili9341::kHeight - 1) * Ili9341::kWidth] == 0x1234u);
}

TEST_CASE_FIXTURE(Ili9341Fix, "SWRESET restores the full 240x320 address window, not ST7789's 240x240") {
    set_window(0, 0, 0, 0);  // narrow it first
    command(0x01);           // SWRESET

    command(0x2C);
    for (unsigned i = 0; i < Ili9341::kWidth; ++i) write_pixel(0x5678);
    CHECK(tft.framebuffer()[Ili9341::kWidth - 1] == 0x5678u);
}
