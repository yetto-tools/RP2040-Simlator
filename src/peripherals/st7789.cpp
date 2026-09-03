#include "peripherals/st7789.h"

#include <algorithm>

namespace rp2040 {

namespace {
constexpr std::uint8_t kCmdSwreset = 0x01;
constexpr std::uint8_t kCmdCaset = 0x2A;
constexpr std::uint8_t kCmdRaset = 0x2B;
constexpr std::uint8_t kCmdRamwr = 0x2C;
constexpr std::uint8_t kCmdMadctl = 0x36;
constexpr std::uint8_t kMadctlBgr = 1u << 3;

// Swap the R/B fields of an RGB565 word - MADCTL.BGR (0x08) tells the real
// controller to route RAMWR's first 5 bits to the panel's blue subpixel
// instead of red (a panel-wiring quirk, not a data-format change), so this
// is what makes framebuffer() always hold "what a person looking at the
// panel would see" regardless of that bit.
std::uint16_t swap_rb565(std::uint16_t v) {
    const std::uint16_t r = static_cast<std::uint16_t>((v >> 11) & 0x1Fu);
    const std::uint16_t g = static_cast<std::uint16_t>((v >> 5) & 0x3Fu);
    const std::uint16_t b = static_cast<std::uint16_t>(v & 0x1Fu);
    return static_cast<std::uint16_t>((b << 11) | (g << 5) | r);
}
}  // namespace

std::uint8_t St7789::on_transfer(std::uint8_t mosi_byte) {
    if (gpio_.level(cs_pin_)) return 0xFFu;  // CS high (active-low): not addressed to us
    if (gpio_.level(dc_pin_)) {
        handle_data(mosi_byte);
    } else {
        handle_command(mosi_byte);
    }
    return 0xFFu;  // no meaningful MISO for a display
}

void St7789::handle_command(std::uint8_t byte) {
    param_index_ = 0;
    switch (byte) {
        case kCmdCaset: cmd_ = Cmd::Caset; break;
        case kCmdRaset: cmd_ = Cmd::Raset; break;
        case kCmdMadctl: cmd_ = Cmd::Madctl; break;
        case kCmdRamwr:
            cmd_ = Cmd::Ramwr;
            col_ = x0_;
            row_ = y0_;
            pixel_high_pending_ = false;
            break;
        case kCmdSwreset:
            // A real reset takes ~120ms and re-blanks GRAM; timing isn't
            // observable through a framebuffer view, but clearing it (and
            // the address window) matches what the user would actually see.
            std::fill(fb_.begin(), fb_.end(), 0u);
            x0_ = 0; x1_ = static_cast<std::uint16_t>(width_ - 1);
            y0_ = 0; y1_ = static_cast<std::uint16_t>(height_ - 1);
            bgr_ = false;
            cmd_ = Cmd::None;
            break;
        default:
            // SLPOUT/NORON/INVON/INVOFF/DISPON/DISPOFF/COLMOD and anything
            // else unrecognised: state-only or ignored (see st7789.h), no
            // parameter bytes tracked.
            cmd_ = Cmd::None;
            break;
    }
}

void St7789::handle_data(std::uint8_t byte) {
    switch (cmd_) {
        case Cmd::Caset:
        case Cmd::Raset: {
            if (param_index_ < params_.size()) params_[param_index_++] = byte;
            if (param_index_ == params_.size()) {
                auto lo = static_cast<std::uint16_t>((params_[0] << 8) | params_[1]);
                auto hi = static_cast<std::uint16_t>((params_[2] << 8) | params_[3]);
                if (hi < lo) std::swap(lo, hi);
                std::uint16_t& start = cmd_ == Cmd::Caset ? x0_ : y0_;
                std::uint16_t& end = cmd_ == Cmd::Caset ? x1_ : y1_;
                const std::uint16_t max = static_cast<std::uint16_t>((cmd_ == Cmd::Caset ? width_ : height_) - 1);
                start = std::min(lo, max);
                end = std::min(hi, max);
            }
            break;
        }
        case Cmd::Madctl:
            bgr_ = (byte & kMadctlBgr) != 0;
            break;
        case Cmd::Ramwr:
            write_pixel_byte(byte);
            break;
        case Cmd::None:
            break;  // stray data with no active multi-byte command
    }
}

void St7789::write_pixel_byte(std::uint8_t byte) {
    if (!pixel_high_pending_) {
        pixel_high_ = byte;
        pixel_high_pending_ = true;
        return;
    }
    pixel_high_pending_ = false;

    const auto raw = static_cast<std::uint16_t>((static_cast<std::uint16_t>(pixel_high_) << 8) | byte);
    if (row_ <= y1_ && col_ <= x1_) {
        fb_[static_cast<std::size_t>(row_) * width_ + col_] = bgr_ ? swap_rb565(raw) : raw;
    }

    if (col_ >= x1_) {
        col_ = x0_;
        row_ = (row_ >= y1_) ? y0_ : static_cast<std::uint16_t>(row_ + 1);
    } else {
        ++col_;
    }
}

}  // namespace rp2040
