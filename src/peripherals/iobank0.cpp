#include "peripherals/iobank0.h"

namespace rp2040 {

BusResult<std::uint32_t> IoBank0::bus_read(std::uint32_t offset, BusWidth) {
    // Each GPIO occupies 8 bytes: +0 = GPIOx_STATUS, +4 = GPIOx_CTRL.
    if (offset < Gpio::kNumPins * 8u) {
        const unsigned pin = offset / 8u;
        if ((offset & 4u) != 0) return {ctrl_[pin], BusStatus::Ok};        // CTRL
        // STATUS: only expose the current output/input levels (bits 9, 17).
        std::uint32_t status = 0;
        if (gpio_.pad_level(pin)) status |= (1u << 9);
        if (gpio_.level(pin))     status |= (1u << 17);
        return {status, BusStatus::Ok};
    }
    return {0u, BusStatus::Ok};
}

BusStatus IoBank0::bus_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    if (offset < Gpio::kNumPins * 8u && (offset & 4u) != 0) {
        const unsigned pin = offset / 8u;
        ctrl_[pin] = value;
        gpio_.set_funcsel(pin, static_cast<std::uint8_t>(value & 0x1Fu));
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
