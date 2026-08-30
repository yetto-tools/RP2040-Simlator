// sysinfo.h - RP2040 SYSINFO block (datasheet 2.20) @ 0x40000000.
//
// A handful of read-only identification registers. pico-sdk reads CHIP_ID
// (rp2040_chip_version()) and PLATFORM during runtime init, so the simulator
// has to answer with plausible silicon values.
#ifndef RP2040_PERIPHERALS_SYSINFO_H
#define RP2040_PERIPHERALS_SYSINFO_H

#include <cstdint>

#include "core/bus.h"
#include "core/memory.h"

namespace rp2040 {

class Sysinfo : public BusPeripheral {
public:
    static constexpr std::uint32_t kBase = 0x40000000u;
    static constexpr std::uint32_t kSize = 0x4000u;

    // CHIP_ID: REVISION[31:28] | PART[27:12] = 0x0002 | MANUFACTURER[11:0] = 0x927.
    // REVISION 2 == RP2040 B1/B2 stepping.
    static constexpr std::uint32_t kChipId = (2u << 28) | (0x0002u << 12) | 0x927u;
    // PLATFORM: bit0 FPGA, bit1 ASIC. Real silicon reads ASIC.
    static constexpr std::uint32_t kPlatformAsic = 1u << 1;
    static constexpr std::uint32_t kGitRef = 0xE1FB4B4Au;  // arbitrary but stable

    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_SYSINFO_H
