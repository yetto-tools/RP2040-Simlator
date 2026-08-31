#include "peripherals/gpio.h"

namespace rp2040 {

void Gpio::set_funcsel(unsigned pin, std::uint8_t funcsel) {
    if (pin < kNumPins) pins_[pin].funcsel = funcsel;
}

std::uint8_t Gpio::funcsel(unsigned pin) const {
    return pin < kNumPins ? pins_[pin].funcsel : static_cast<std::uint8_t>(kFuncNull);
}

int Gpio::driver_index(unsigned pin) const {
    if (pin >= kNumPins) return -1;
    switch (pins_[pin].funcsel) {
        case kFuncSio:  return kSio;
        case kFuncPio0: return kPio0;
        case kFuncPio1: return kPio1;
        case kFuncPwm:  return kPwm;
        default:        return -1;
    }
}

void Gpio::driver_set_pin(Driver d, unsigned pin, bool level) {
    if (pin >= kNumPins) return;
    const std::uint32_t bit = 1u << pin;
    if (level) out_[d] |= bit; else out_[d] &= ~bit;
}

void Gpio::driver_set_pindir(Driver d, unsigned pin, bool output) {
    if (pin >= kNumPins) return;
    const std::uint32_t bit = 1u << pin;
    if (output) oe_[d] |= bit; else oe_[d] &= ~bit;
}

void Gpio::set_pulls(unsigned pin, bool pull_up, bool pull_down) {
    if (pin >= kNumPins) return;
    pins_[pin].pull_up = pull_up;
    pins_[pin].pull_down = pull_down;
}

void Gpio::set_overrides(unsigned pin, std::uint8_t out_over, std::uint8_t oe_over,
                         std::uint8_t in_over, std::uint8_t irq_over) {
    if (pin >= kNumPins) return;
    pins_[pin].out_over = out_over & 0x3u;
    pins_[pin].oe_over = oe_over & 0x3u;
    pins_[pin].in_over = in_over & 0x3u;
    pins_[pin].irq_over = irq_over & 0x3u;
}

bool Gpio::apply_override(std::uint8_t sel, bool value) {
    switch (sel & 0x3u) {
        case kOverInvert: return !value;
        case kOverLow:    return false;
        case kOverHigh:   return true;
        default:          return value;
    }
}

void Gpio::set_external(unsigned pin, bool level) {
    if (pin >= kNumPins) return;
    pins_[pin].ext_driven = true;
    pins_[pin].ext_level = level;
}

void Gpio::clear_external(unsigned pin) {
    if (pin < kNumPins) pins_[pin].ext_driven = false;
}

bool Gpio::pad_driving(unsigned pin) const {
    if (pin >= kNumPins) return false;
    const int d = driver_index(pin);
    const bool func_oe = d >= 0 && ((oe_[static_cast<unsigned>(d)] >> pin) & 1u) != 0;
    return apply_override(pins_[pin].oe_over, func_oe);
}

bool Gpio::pad_level(unsigned pin) const {
    if (pin >= kNumPins) return false;
    const int d = driver_index(pin);
    const bool func_out = d >= 0 && ((out_[static_cast<unsigned>(d)] >> pin) & 1u) != 0;
    return apply_override(pins_[pin].out_over, func_out);
}

bool Gpio::level(unsigned pin) const {
    if (pin >= kNumPins) return false;
    if (pad_driving(pin)) return pad_level(pin);       // pad senses its own output
    const Pin& p = pins_[pin];
    if (p.ext_driven) return p.ext_level;
    if (p.pull_up) return true;
    if (p.pull_down) return false;
    return false;  // floating input modelled as 0
}

bool Gpio::func_level(unsigned pin) const {
    if (pin >= kNumPins) return false;
    return apply_override(pins_[pin].in_over, level(pin));
}

bool Gpio::irq_level(unsigned pin) const {
    if (pin >= kNumPins) return false;
    return apply_override(pins_[pin].irq_over, level(pin));
}

std::uint32_t Gpio::input_bits() const {
    std::uint32_t v = 0;
    for (unsigned pin = 0; pin < kNumPins; ++pin) {
        if (level(pin)) v |= (1u << pin);
    }
    return v;
}

std::uint32_t Gpio::func_input_bits() const {
    std::uint32_t v = 0;
    for (unsigned pin = 0; pin < kNumPins; ++pin) {
        if (func_level(pin)) v |= (1u << pin);
    }
    return v;
}

}  // namespace rp2040
