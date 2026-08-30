// resets.h - RP2040 RESETS block (datasheet 2.14) @ 0x4000C000.
//
// One bit per peripheral: RESET holds it in reset, RESET_DONE reads back the
// peripherals that are out of reset and ready. The simulator's peripherals
// are always "ready", so RESET_DONE is simply ~RESET - this lets pico-sdk's
// unreset_block_wait() return immediately.
#ifndef RP2040_PERIPHERALS_RESETS_H
#define RP2040_PERIPHERALS_RESETS_H

#include <cstdint>

#include "core/bus.h"
#include "core/memory.h"

namespace rp2040 {

class Resets : public BusPeripheral {
public:
    static constexpr std::uint32_t kBase = 0x4000C000u;
    // 0x1000 of registers + the +0x1000/2000/3000 XOR/SET/CLR alias windows.
    static constexpr std::uint32_t kSize = 0x4000u;

    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;

private:
    std::uint32_t reset_ = 0x01FFFFFFu;  // reset value: everything held in reset
    std::uint32_t wdsel_ = 0;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_RESETS_H
