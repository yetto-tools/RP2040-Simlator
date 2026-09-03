// ssd1306.h - SSD1306 I2C OLED controller, 128x64 1bpp module.
//
// Not an RP2040 peripheral - a virtual *device* the web lab's circuit editor
// can wire up, registered as an I2c::SlaveFn (i2c.h) at the address the user
// chose. Unlike ST7789 (SPI, framed by bit-banged CS/DC), the SSD1306's
// framing lives entirely inside the I2C byte stream itself: each transaction
// starts with a control byte (bit7 = Co "another control byte follows", bit6
// = D/C# "command vs data") and, per datasheet 8.1.5.2/Fig 16, only re-reads
// a control byte when Co said to or a fresh START (STOP-then-START) begins.
// Hence the extra I2c::on_stop() hook this class needs: a STOP always resets
// the "expect a control byte next" state, regardless of the last Co seen.
//
// Command coverage matches what MicroPython's ssd1306.py driver actually
// sends: SET_DISP (on/off), SET_CONTRAST/SET_MEM_ADDR/SET_MUX_RATIO/
// SET_DISP_OFFSET/SET_COM_PIN_CFG/SET_DISP_CLK_DIV/SET_PRECHARGE/
// SET_VCOM_DESEL/SET_CHARGE_PUMP (parameter bytes consumed, no framebuffer
// effect - this project's "document the approximation" rule covers the
// rest), SET_SEG_REMAP/SET_COM_OUT_DIR/SET_DISP_START_LINE/SET_ENTIRE_ON
// (consumed, no effect), SET_NORM_INV (tracked - inverts the exposed
// framebuffer), SET_COL_ADDR/SET_PAGE_ADDR (16-bit-per-axis GDDRAM window,
// horizontal addressing mode only - the only mode the driver configures),
// and GDDRAM data writes (auto-incrementing/wrapping within that window,
// matching the real page-major layout byte-for-byte).
#ifndef RP2040_PERIPHERALS_SSD1306_H
#define RP2040_PERIPHERALS_SSD1306_H

#include <cstdint>
#include <vector>

namespace rp2040 {

class Ssd1306 {
public:
    static constexpr unsigned kWidth = 128;
    static constexpr unsigned kHeight = 64;

    // I2c::SlaveFn-shaped (i2c.h): `i2c.set_slave(addr, [&](bool is_read,
    // std::uint8_t& b) { return oled.on_transfer(is_read, b); });`. Reads
    // aren't part of any real driver's protocol (status-byte polling isn't
    // used by ssd1306.py), so a read always NACKs.
    bool on_transfer(bool is_read, std::uint8_t& byte);

    // Wire to I2c::on_stop(...) - resets the control-byte framing state so
    // the next transaction starts fresh, matching real I2C START/STOP
    // semantics (see the file comment above).
    void on_stop();

    // Page-major GDDRAM, byte-for-byte matching hardware: gddram()[col +
    // page * kWidth], each byte's bit N = pixel (col, page*8 + N). Same
    // layout the MicroPython framebuf MVLSB format and the real controller
    // both use, so a viewer can unpack it the same way either would.
    const std::vector<std::uint8_t>& gddram() const { return gddram_; }

    bool display_on() const { return display_on_; }
    bool inverted() const { return inverted_; }

private:
    enum class Awaiting {
        None, MemAddrMode, ColStart, ColEnd, PageStart, PageEnd,
        MuxRatio, DispOffset, ComPinCfg, ClkDiv, Precharge, VcomDesel,
        Contrast, ChargePump,
    };

    void handle_command(std::uint8_t byte);
    void handle_data(std::uint8_t byte);

    // I2C byte-framing state (reset by on_stop()).
    bool expect_control_ = true;
    bool co_ = false;
    bool dc_ = false;

    Awaiting awaiting_ = Awaiting::None;

    std::uint8_t col_start_ = 0, col_end_ = kWidth - 1;
    std::uint8_t page_start_ = 0, page_end_ = kHeight / 8 - 1;
    std::uint8_t cur_col_ = 0, cur_page_ = 0;

    bool display_on_ = false;
    bool inverted_ = false;

    std::vector<std::uint8_t> gddram_ =
        std::vector<std::uint8_t>(static_cast<std::size_t>(kWidth) * (kHeight / 8), 0);
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_SSD1306_H
