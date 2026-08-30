// atomic_peripheral.h - base for peripherals that expose the RP2040 register
// atomic aliases (datasheet 2.1.2).
//
// The register block appears four times in a 0x4000 window:
//   +0x0000  normal read / write
//   +0x1000  atomic XOR   (reg ^= value)
//   +0x2000  atomic bit-set (reg |= value)
//   +0x3000  atomic bit-clear (reg &= ~value)
//
// A subclass implements reg_read()/reg_write() on the 0x000-0xFFF offset and
// gets the aliases for free (as a read-modify-write). This is safe for
// configuration registers; peripherals with read/write side effects on data
// registers (FIFOs) should not be accessed through the aliases anyway.
#ifndef RP2040_CORE_ATOMIC_PERIPHERAL_H
#define RP2040_CORE_ATOMIC_PERIPHERAL_H

#include <cstdint>

#include "core/bus.h"

namespace rp2040 {

class AtomicPeripheral : public BusPeripheral {
public:
    static constexpr std::uint32_t kAtomicSize = 0x4000u;

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) final {
        return reg_read(offset & 0x0FFFu, w);
    }

    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) final {
        const std::uint32_t reg = offset & 0x0FFFu;
        switch (offset & 0x3000u) {
            case 0x1000u: value = reg_read(reg, w).value ^ value; break;   // XOR
            case 0x2000u: value = reg_read(reg, w).value | value; break;   // SET
            case 0x3000u: value = reg_read(reg, w).value & ~value; break;  // CLR
            default: break;                                               // normal
        }
        return reg_write(reg, value, w);
    }

protected:
    virtual BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth w) = 0;
    virtual BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth w) = 0;
};

}  // namespace rp2040

#endif  // RP2040_CORE_ATOMIC_PERIPHERAL_H
