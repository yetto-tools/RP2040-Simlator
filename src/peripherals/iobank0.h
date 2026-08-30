// iobank0.h - IO_BANK0: per-GPIO function select and overrides (datasheet
// 2.19.6.1). Only GPIOx_CTRL.FUNCSEL is modelled so far; overrides, STATUS
// and the per-pin interrupt config land with the GPIO interrupt slice.
#ifndef RP2040_PERIPHERALS_IOBANK0_H
#define RP2040_PERIPHERALS_IOBANK0_H

#include <array>
#include <cstdint>

#include "core/bus.h"
#include "core/memory.h"
#include "peripherals/gpio.h"

namespace rp2040 {

class IoBank0 : public BusPeripheral {
public:
    static constexpr std::uint32_t kBase = 0x40014000u;
    static constexpr std::uint32_t kSize = 0x4000u;

    explicit IoBank0(Gpio& gpio) : gpio_(gpio) {}
    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;

private:
    Gpio& gpio_;
    std::array<std::uint32_t, Gpio::kNumPins> ctrl_{};  // last-written GPIOx_CTRL
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_IOBANK0_H
