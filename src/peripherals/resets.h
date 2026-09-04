// resets.h - RP2040 RESETS block (datasheet 2.14) @ 0x4000C000.
//
// One bit per peripheral: RESET holds it in reset, RESET_DONE reads back the
// peripherals that are out of reset and ready. The simulator's peripherals
// are always "ready", so RESET_DONE is simply ~RESET - this lets pico-sdk's
// unreset_block_wait() return immediately.
#ifndef RP2040_PERIPHERALS_RESETS_H
#define RP2040_PERIPHERALS_RESETS_H

#include <cstdint>

#include "core/atomic_peripheral.h"
#include "core/bus.h"
#include "core/memory.h"

namespace rp2040 {

class Resets : public AtomicPeripheral {
public:
    static constexpr std::uint32_t kBase = 0x4000C000u;
    static constexpr std::uint32_t kSize = AtomicPeripheral::kAtomicSize;

    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }

    BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth w) override;
    BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth w) override;

    // Live RESETS_WDSEL value (queried by Watchdog on every reset - see
    // Simulator's set_resets_wdsel_provider() wiring).
    std::uint32_t wdsel() const { return wdsel_; }

private:
    std::uint32_t reset_ = 0x01FFFFFFu;  // reset value: everything held in reset
    std::uint32_t wdsel_ = 0;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_RESETS_H
