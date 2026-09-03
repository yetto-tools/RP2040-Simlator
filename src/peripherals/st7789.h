// st7789.h - ST7789(V/S) SPI TFT controller, 1.3" 240x240 RGB565 module.
// Also the shared base for Ili9341 (ili9341.h) - see the width/height ctor
// params below.
//
// Not an RP2040 peripheral - a virtual *device* the web lab's circuit editor
// can wire up, registered as an Spi::SlaveFn (spi.h) at the pins the user
// chose for SCK/MOSI/CS/DC. CS/DC are ordinary bit-banged GPIOs (Spi itself
// doesn't model chip-select - see spi.h), so this class holds a Gpio&
// reference and reads both pins' live level on every transferred byte to
// decide whether it's addressed to us (CS active-low) and whether it's a
// command or data/parameter byte (DC).
//
// Command coverage is deliberately scoped to what real ST7789 drivers
// actually send (MicroPython's st7789py, pico-sdk examples, etc.), not the
// full datasheet: SWRESET/SLPOUT/NORON/INVON/INVOFF/DISPON/DISPOFF (state
// only, no framebuffer effect), COLMOD (ignored - always treated as 16bpp/
// RGB565, the only format a framebuffer view needs), MADCTL (only the BGR
// bit, 0x08, is honoured - row/column swap and mirroring (MY/MX/MV) are
// not; most fill/blit code doesn't depend on them and this project's own
// "document the approximation" rule covers the rest), CASET/RASET (16-bit
// column/row address window), RAMWR (streams RGB565 pixels into that
// window, wrapping back to its start - real drivers never send more pixels
// than the window holds, so wrap-instead-of-stop is unobservable).
//
// This command set is standard MIPI DBI Type C, not ST7789-specific -
// ILI9341 (and most other small SPI TFT controllers) use the same opcodes
// for every command implemented here, differing mainly in panel resolution.
// Ili9341 (ili9341.h) is a thin subclass that only changes the width/height
// passed to this constructor - no command-decode duplication.
#ifndef RP2040_PERIPHERALS_ST7789_H
#define RP2040_PERIPHERALS_ST7789_H

#include <array>
#include <cstdint>
#include <vector>

#include "peripherals/gpio.h"

namespace rp2040 {

class St7789 {
public:
    static constexpr unsigned kWidth = 240;
    static constexpr unsigned kHeight = 240;

    // width/height let a subclass (Ili9341) reuse this decoder against a
    // different panel resolution; ST7789 users leave them at the defaults.
    St7789(Gpio& gpio, unsigned cs_pin, unsigned dc_pin, unsigned width = kWidth, unsigned height = kHeight)
        : gpio_(gpio), cs_pin_(cs_pin), dc_pin_(dc_pin), width_(width), height_(height),
          x1_(static_cast<std::uint16_t>(width_ - 1)), y1_(static_cast<std::uint16_t>(height_ - 1)),
          fb_(static_cast<std::size_t>(width_) * height_, 0) {}

    // Spi::SlaveFn-shaped (spi.h): `spi.on_transfer([&](std::uint8_t b) {
    // return st7789.on_transfer(b); });`. This display drives no meaningful
    // MISO, so the return value is always the PL022's idle-bus byte (0xFF,
    // matching Spi's own "no slave attached" default).
    std::uint8_t on_transfer(std::uint8_t mosi_byte);

    // Already re-ordered to true RGB565 (R:5 G:6 B:5, MSB first) regardless
    // of the panel's MADCTL.BGR setting - see handle_data()'s Madctl case.
    const std::vector<std::uint16_t>& framebuffer() const { return fb_; }

private:
    enum class Cmd : std::uint8_t { None, Caset, Raset, Madctl, Ramwr };

    void handle_command(std::uint8_t byte);
    void handle_data(std::uint8_t byte);
    void write_pixel_byte(std::uint8_t byte);

    Gpio& gpio_;
    unsigned cs_pin_;
    unsigned dc_pin_;
    unsigned width_;
    unsigned height_;

    Cmd cmd_ = Cmd::None;
    unsigned param_index_ = 0;
    std::array<std::uint8_t, 4> params_{};

    std::uint16_t x0_ = 0, x1_;
    std::uint16_t y0_ = 0, y1_;
    std::uint16_t col_ = 0, row_ = 0;
    bool pixel_high_pending_ = false;
    std::uint8_t pixel_high_ = 0;
    bool bgr_ = false;  // ST7789 silicon reset default is RGB; a real init sets MADCTL if the panel needs BGR.

    std::vector<std::uint16_t> fb_;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_ST7789_H
