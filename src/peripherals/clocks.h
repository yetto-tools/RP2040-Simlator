// clocks.h - minimal RP2040 clock-tree peripherals so pico-sdk's clocks_init
// runs: XOSC, PLL_SYS / PLL_USB, and the CLOCKS block (datasheet 2.15-2.18).
//
// The simulator derives all timing from a fixed system frequency, so these
// only need to accept the configuration writes and report "stable" / "locked"
// / the selected mux so firmware does not spin.
#ifndef RP2040_PERIPHERALS_CLOCKS_H
#define RP2040_PERIPHERALS_CLOCKS_H

#include <array>
#include <cstdint>

#include "core/atomic_peripheral.h"
#include "core/bus.h"
#include "core/memory.h"

namespace rp2040 {

class Xosc : public AtomicPeripheral {
public:
    static constexpr std::uint32_t kBase = 0x40024000u;
    static constexpr std::uint32_t kSize = AtomicPeripheral::kAtomicSize;
    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }
    BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth w) override;
    BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth w) override;

private:
    std::uint32_t ctrl_ = 0;
    std::uint32_t startup_ = 0;
};

class Pll : public AtomicPeripheral {
public:
    static constexpr std::uint32_t kSysBase = 0x40028000u;
    static constexpr std::uint32_t kUsbBase = 0x4002C000u;
    static constexpr std::uint32_t kSize = AtomicPeripheral::kAtomicSize;
    explicit Pll(std::uint32_t base) : base_(base) {}
    bool attach(Memory& mem) { return mem.attach_peripheral(base_, kSize, this); }
    BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth w) override;
    BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth w) override;

private:
    std::uint32_t base_;
    std::uint32_t cs_ = 0;
    std::uint32_t pwr_ = 0xFFFFFFFFu;
    std::uint32_t fbdiv_ = 0;
    std::uint32_t prim_ = 0x00070700u;
};

class Clocks : public AtomicPeripheral {
public:
    static constexpr std::uint32_t kBase = 0x40008000u;
    static constexpr std::uint32_t kSize = AtomicPeripheral::kAtomicSize;
    static constexpr unsigned kNumGenerators = 10;
    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }
    BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth w) override;
    BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth w) override;

private:
    struct Gen { std::uint32_t ctrl = 0, div = 0x00010000u; };
    std::array<Gen, kNumGenerators> gen_{};
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_CLOCKS_H
