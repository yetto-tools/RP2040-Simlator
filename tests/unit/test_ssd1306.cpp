// Unit tests for the SSD1306 I2C OLED virtual device (ssd1306.h) - a circuit
// component the web lab wires up, not an RP2040 peripheral. Driven through
// I2c directly (matching test_i2c.cpp's style), reproducing the exact
// transaction framing MicroPython's ssd1306.py driver uses: write_cmd sends
// a 2-byte [0x80, cmd] transaction per command byte; write_data sends one
// [0x40, ...buffer] transaction for the whole framebuffer.
#include "doctest.h"

#include <cstdint>
#include <vector>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "peripherals/i2c.h"
#include "peripherals/ssd1306.h"

using namespace rp2040;

namespace {

constexpr std::uint8_t kAddr = 0x3C;

struct Ssd1306Fix {
    RegisterFile regs;
    Memory mem;
    Cpu cpu{regs, mem};
    I2c i2c{cpu, I2c::kI2c0Base, I2c::kI2c0Irq};
    Ssd1306 oled;

    Ssd1306Fix() {
        REQUIRE(i2c.attach(mem));
        wr(0x1C, 2u);   // IC_FS_SCL_HCNT
        wr(0x20, 2u);   // IC_FS_SCL_LCNT -> SCL period = 4 ic_clk cycles
        wr(0x6C, 1u);   // IC_ENABLE
        i2c.set_slave(kAddr, [this](bool is_read, std::uint8_t& b) { return oled.on_transfer(is_read, b); });
        i2c.on_stop([this]() { oled.on_stop(); });
        wr(0x04, kAddr);  // IC_TAR
    }

    std::uint32_t rd(std::uint32_t off) { return mem.read_word(I2c::kI2c0Base + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(I2c::kI2c0Base + off, v) == BusStatus::Ok);
    }
    // One byte transaction = 9 SCL periods (8 data bits + ACK) * 4 cyc/period
    // = 36 ic_clk cycles; 50 gives comfortable margin.
    void advance_cmds(int n) { i2c.on_cycles(static_cast<std::uint64_t>(n) * 50u); }

    // Matches ssd1306.py's write_cmd: control byte 0x80 (Co=1,D/C#=0) then
    // the command byte, as their own 2-byte transaction ending in STOP.
    void write_cmd(std::uint8_t cmd) {
        wr(0x10, 0x80);
        wr(0x10, cmd | (1u << 9));
        advance_cmds(2);
    }

    // Matches ssd1306.py's write_data: control byte 0x40 (Co=0,D/C#=1) then
    // the whole buffer, as one transaction ending in STOP.
    void write_data(const std::vector<std::uint8_t>& data) {
        wr(0x10, 0x40);
        for (std::size_t i = 0; i < data.size(); ++i) {
            const bool stop = (i + 1 == data.size());
            wr(0x10, data[i] | (stop ? (1u << 9) : 0u));
        }
        advance_cmds(static_cast<int>(data.size()) + 1);
    }

    void set_window(std::uint8_t col0, std::uint8_t col1, std::uint8_t page0, std::uint8_t page1) {
        write_cmd(0x21);  // SET_COL_ADDR
        write_cmd(col0);
        write_cmd(col1);
        write_cmd(0x22);  // SET_PAGE_ADDR
        write_cmd(page0);
        write_cmd(page1);
    }
};

}  // namespace

TEST_CASE_FIXTURE(Ssd1306Fix, "SET_DISP turns the display on and off") {
    CHECK_FALSE(oled.display_on());
    write_cmd(0xAF);  // SET_DISP | on
    CHECK(oled.display_on());
    write_cmd(0xAE);  // SET_DISP | off
    CHECK_FALSE(oled.display_on());
}

TEST_CASE_FIXTURE(Ssd1306Fix, "a full-window data write fills GDDRAM in page-major order") {
    set_window(0, Ssd1306::kWidth - 1, 0, Ssd1306::kHeight / 8 - 1);
    write_data({0x11, 0x22, 0x33});

    const auto& g = oled.gddram();
    CHECK(g[0] == 0x11u);
    CHECK(g[1] == 0x22u);
    CHECK(g[2] == 0x33u);
}

TEST_CASE_FIXTURE(Ssd1306Fix, "a sub-window wraps back to its own start, not the next page") {
    set_window(2, 3, 1, 1);  // a 2-column window on page 1
    write_data({0xAA, 0xBB, 0xCC});  // wraps back to column 2 for the 3rd byte

    const auto& g = oled.gddram();
    const std::size_t page1 = 1u * Ssd1306::kWidth;
    CHECK(g[page1 + 2] == 0xCCu);   // overwritten by the wrapped byte
    CHECK(g[page1 + 3] == 0xBBu);
    CHECK(g[page1 + 4] == 0u);      // outside the window: untouched
}

TEST_CASE_FIXTURE(Ssd1306Fix, "SET_NORM_INV is tracked") {
    CHECK_FALSE(oled.inverted());
    write_cmd(0xA7);  // SET_NORM_INV | invert
    CHECK(oled.inverted());
    write_cmd(0xA6);
    CHECK_FALSE(oled.inverted());
}

TEST_CASE_FIXTURE(Ssd1306Fix, "single-parameter setup commands are consumed without side effects") {
    write_cmd(0x81);  // SET_CONTRAST
    write_cmd(0xFF);
    write_cmd(0x8D);  // SET_CHARGE_PUMP
    write_cmd(0x14);
    write_cmd(0xA8);  // SET_MUX_RATIO
    write_cmd(0x3F);
    // No crash, no unexpected GDDRAM writes, and normal command decoding
    // still works right after.
    CHECK_FALSE(oled.display_on());
    write_cmd(0xAF);
    CHECK(oled.display_on());
}

TEST_CASE_FIXTURE(Ssd1306Fix, "a STOP resets control-byte framing even mid-stream") {
    set_window(0, Ssd1306::kWidth - 1, 0, Ssd1306::kHeight / 8 - 1);
    write_data({0x99});  // a data transaction, terminated by STOP

    // If STOP hadn't reset framing, this first byte of the next transaction
    // (which real hardware always expects to be a control byte) would be
    // misread as raw GDDRAM data instead of a control byte.
    write_cmd(0xAF);
    CHECK(oled.display_on());
    CHECK(oled.gddram()[1] == 0u);  // no stray write past the first byte
}
