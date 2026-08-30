// padsbank0.h - PADS_BANK0: per-pad electrical config (datasheet 2.19.6.3).
// Only the pull-up / pull-down enables are wired to the Gpio model; drive
// strength, slew and Schmitt are stored for read-back.
#ifndef RP2040_PERIPHERALS_PADSBANK0_H
#define RP2040_PERIPHERALS_PADSBANK0_H

#include <array>
#include <cstdint>

#include "core/atomic_peripheral.h"
#include "core/bus.h"
#include "core/memory.h"
#include "peripherals/gpio.h"

namespace rp2040 {

class PadsBank0 : public AtomicPeripheral {
public:
    static constexpr std::uint32_t kBase = 0x4001C000u;
    static constexpr std::uint32_t kSize = AtomicPeripheral::kAtomicSize;

    explicit PadsBank0(Gpio& gpio) : gpio_(gpio) { pad_.fill(0x56u); }
    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }

    BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth w) override;
    BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth w) override;

private:
    Gpio& gpio_;
    std::uint32_t voltage_select_ = 0;
    // reset value 0x56: IE=1, DRIVE=1 (4mA), PDE=1, others 0
    std::array<std::uint32_t, Gpio::kNumPins> pad_{};
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_PADSBANK0_H
