// gpio.h - RP2040 GPIO bank 0 pad/function model (datasheet 2.19).
//
// A pad's behaviour is the combination of: which function drives it
// (IO_BANK0 FUNCSEL - SIO, PIO0, PIO1, ...), that function's output value and
// output-enable, the pad pulls, and whatever the outside world drives onto
// the pin. This class is the single source of truth every driver shares.
#ifndef RP2040_PERIPHERALS_GPIO_H
#define RP2040_PERIPHERALS_GPIO_H

#include <array>
#include <cstdint>

namespace rp2040 {

class Gpio {
public:
    static constexpr int kNumPins = 30;                 // GPIO0..29
    static constexpr std::uint32_t kPinMask = (1u << kNumPins) - 1u;

    // FUNCSEL values (datasheet 2.19.2, Table 289). Only the functions the
    // simulator currently models are named.
    enum Funcsel : std::uint8_t {
        kFuncSpi = 1, kFuncUart = 2, kFuncI2c = 3, kFuncPwm = 4,
        kFuncSio = 5, kFuncPio0 = 6, kFuncPio1 = 7,
        kFuncNull = 0x1F,
    };

    // Internal driver identity for the modelled functions.
    enum Driver : std::uint8_t {
        kSio = 0, kPio0 = 1, kPio1 = 2, kPwm = 3, kSpi0 = 4, kSpi1 = 5, kNumDrivers = 6
    };

    // --- function select ------------------------------------------------
    void set_funcsel(unsigned pin, std::uint8_t funcsel);
    std::uint8_t funcsel(unsigned pin) const;

    // --- a driver presents an output --------------------------------------
    void driver_set_out(Driver d, std::uint32_t value) { out_[d] = value & kPinMask; }
    void driver_set_oe(Driver d, std::uint32_t value) { oe_[d] = value & kPinMask; }
    std::uint32_t driver_out(Driver d) const { return out_[d]; }
    std::uint32_t driver_oe(Driver d) const { return oe_[d]; }
    void driver_set_pin(Driver d, unsigned pin, bool level);
    void driver_set_pindir(Driver d, unsigned pin, bool output);

    // --- pad pulls --------------------------------------------------------
    void set_pulls(unsigned pin, bool pull_up, bool pull_down);

    // --- GPIOx_CTRL overrides (datasheet 2.19.6.1) ----------------------
    // Each is a 2-bit selector: 0 = normal, 1 = invert, 2 = force low,
    // 3 = force high.
    enum Override : std::uint8_t { kOverNormal = 0, kOverInvert = 1, kOverLow = 2, kOverHigh = 3 };
    void set_overrides(unsigned pin, std::uint8_t out_over, std::uint8_t oe_over,
                       std::uint8_t in_over, std::uint8_t irq_over);

    // --- external stimulus (test bench / attached device) ---------------
    void set_external(unsigned pin, bool level);
    void clear_external(unsigned pin);          // pin goes hi-Z, pulls decide

    // --- effective values ----------------------------------------------
    bool pad_driving(unsigned pin) const;       // is the pad actively driving? (OEOVER applied)
    bool pad_level(unsigned pin) const;         // value the pad drives (OUTOVER applied)
    bool level(unsigned pin) const;             // raw value at the pin
    bool func_level(unsigned pin) const;        // value a peripheral input sees (INOVER applied)
    bool irq_level(unsigned pin) const;         // value the IO_BANK0 IRQ block sees (IRQOVER)
    std::uint32_t input_bits() const;           // all pins as a bit vector (raw)
    std::uint32_t func_input_bits() const;      // all pins, INOVER applied (SIO GPIO_IN)

private:
    int driver_index(unsigned pin) const;      // -1 if no modelled function

    static bool apply_override(std::uint8_t sel, bool value);

    struct Pin {
        std::uint8_t funcsel = kFuncNull;
        bool pull_up = false;
        bool pull_down = false;
        bool ext_driven = false;
        bool ext_level = false;
        std::uint8_t out_over = 0;
        std::uint8_t oe_over = 0;
        std::uint8_t in_over = 0;
        std::uint8_t irq_over = 0;
    };
    std::array<Pin, kNumPins> pins_{};
    std::array<std::uint32_t, kNumDrivers> out_{};
    std::array<std::uint32_t, kNumDrivers> oe_{};
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_GPIO_H
