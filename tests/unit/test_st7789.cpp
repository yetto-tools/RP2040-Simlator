// Unit tests for the ST7789 SPI TFT virtual device (st7789.h) - a circuit
// component the web lab wires up, not an RP2040 peripheral. Driven through
// Spi directly (matching test_spi.cpp's style) with CS/DC faked via
// Gpio::set_external, the same test-bench hook PinPanel/circuit-editor
// buttons use in the real app.
#include "doctest.h"

#include <cstdint>
#include <vector>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "peripherals/gpio.h"
#include "peripherals/spi.h"
#include "peripherals/st7789.h"

using namespace rp2040;

namespace {

constexpr unsigned kCsPin = 5;
constexpr unsigned kDcPin = 6;

struct St7789Fix {
    RegisterFile regs;
    Memory mem;
    Cpu cpu{regs, mem};
    Spi spi{cpu, Spi::kSpi0Base, Spi::kSpi0Irq};
    Gpio gpio;
    St7789 tft{gpio, kCsPin, kDcPin};

    St7789Fix() {
        REQUIRE(spi.attach(mem));
        wr(0x00, 0x07);   // SSPCR0: DSS=8 bit, SCR=0
        wr(0x10, 2u);     // SSPCPSR minimum valid divisor
        wr(0x04, 1u << 1);   // SSPCR1.SSE enable
        spi.on_transfer([this](std::uint8_t b) { return tft.on_transfer(b); });
        select();  // CS asserted (active-low) by default so tests don't have to opt in
    }

    std::uint32_t rd(std::uint32_t off) { return mem.read_word(Spi::kSpi0Base + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(Spi::kSpi0Base + off, v) == BusStatus::Ok);
    }
    void select() { gpio.set_external(kCsPin, false); }      // active-low: low = selected
    void deselect() { gpio.set_external(kCsPin, true); }
    void dc_command() { gpio.set_external(kDcPin, false); }
    void dc_data() { gpio.set_external(kDcPin, true); }

    // One 8-bit frame = 8 bits * CPSDVSR(2) = 16 SSPCLK cycles; 30 gives margin.
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
        command(0x2A);  // CASET
        data(static_cast<std::uint8_t>(x0 >> 8));
        data(static_cast<std::uint8_t>(x0));
        data(static_cast<std::uint8_t>(x1 >> 8));
        data(static_cast<std::uint8_t>(x1));
        command(0x2B);  // RASET
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

TEST_CASE_FIXTURE(St7789Fix, "a full-window RAMWR fills the framebuffer in raster order") {
    set_window(0, St7789::kWidth - 1, 0, St7789::kHeight - 1);
    command(0x2C);  // RAMWR
    write_pixel(0xF800);  // pure red, RGB565
    write_pixel(0x07E0);  // pure green

    const auto& fb = tft.framebuffer();
    CHECK(fb[0] == 0xF800u);
    CHECK(fb[1] == 0x07E0u);
}

TEST_CASE_FIXTURE(St7789Fix, "a sub-window wraps back to its own start, not the next row of the panel") {
    set_window(2, 3, 5, 5);  // a 2x1 window at row 5, columns 2..3
    command(0x2C);
    write_pixel(0x1111);
    write_pixel(0x2222);
    write_pixel(0x3333);  // wraps back to column 2 of the same window

    const auto& fb = tft.framebuffer();
    const std::size_t row5 = 5u * St7789::kWidth;
    CHECK(fb[row5 + 2] == 0x3333u);   // overwritten by the wrapped pixel
    CHECK(fb[row5 + 3] == 0x2222u);
    CHECK(fb[row5 + 4] == 0u);        // outside the window: untouched
}

TEST_CASE_FIXTURE(St7789Fix, "MADCTL's BGR bit swaps the red/blue fields for display") {
    command(0x36);  // MADCTL
    data(0x08);      // BGR bit set
    set_window(0, 0, 0, 0);
    command(0x2C);
    write_pixel(0xF800);  // "red" as sent - but the panel is wired BGR

    // Swapped so framebuffer() always holds what a viewer actually sees:
    // the R field (bits 15:11) and B field (bits 4:0) trade places.
    CHECK(tft.framebuffer()[0] == 0x001Fu);  // pure blue once corrected
}

TEST_CASE_FIXTURE(St7789Fix, "deselected (CS high) transfers are ignored") {
    deselect();
    command(0x36);
    data(0x08);
    set_window(0, 0, 0, 0);
    command(0x2C);
    write_pixel(0xFFFF);

    CHECK(tft.framebuffer()[0] == 0u);  // nothing landed - we were never selected
}

TEST_CASE_FIXTURE(St7789Fix, "SWRESET clears the framebuffer and resets the address window") {
    set_window(0, 0, 0, 0);  // a narrow 1x1 window
    command(0x2C);
    write_pixel(0xFFFF);
    REQUIRE(tft.framebuffer()[0] == 0xFFFFu);

    command(0x01);  // SWRESET
    CHECK(tft.framebuffer()[0] == 0u);

    // The window is back to full-screen: writing kWidth pixels should reach
    // column kWidth-1 of row 0, not immediately wrap at column 0 the way
    // the narrow 1x1 window above would.
    command(0x2C);
    for (unsigned i = 0; i < St7789::kWidth; ++i) write_pixel(0x1234);
    CHECK(tft.framebuffer()[St7789::kWidth - 1] == 0x1234u);
}
