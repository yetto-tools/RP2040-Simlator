// ili9341.h - ILI9341 SPI TFT controller, 2.2"-2.8" 240x320 RGB565 modules.
//
// Not a separate emulator - a thin St7789 (st7789.h) specialization. Both
// controllers implement the same MIPI DBI Type C command set for every
// command st7789.cpp decodes (SWRESET/SLPOUT/NORON/INVON/INVOFF/DISPON/
// DISPOFF/COLMOD/MADCTL/CASET/RASET/RAMWR); the only thing that differs is
// panel resolution (240x320 here vs ST7789's 240x240), which St7789's
// constructor already takes as a parameter. See st7789.h's file comment for
// full command-coverage details and approximations - they all apply here
// unchanged.
#ifndef RP2040_PERIPHERALS_ILI9341_H
#define RP2040_PERIPHERALS_ILI9341_H

#include "peripherals/st7789.h"

namespace rp2040 {

class Ili9341 : public St7789 {
public:
    static constexpr unsigned kWidth = 240;
    static constexpr unsigned kHeight = 320;

    Ili9341(Gpio& gpio, unsigned cs_pin, unsigned dc_pin) : St7789(gpio, cs_pin, dc_pin, kWidth, kHeight) {}
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_ILI9341_H
