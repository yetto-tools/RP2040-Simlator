// watchdog.h - RP2040 Watchdog (datasheet 4.7) @ 0x40058000.
//
// A down-counter that, left un-fed, resets the machine. Per the documented
// hardware behaviour the counter decrements by 2 per microsecond tick.
// SCRATCH0-7 survive the reset. on_cycles() paces the tick from the system
// clock; a timeout invokes the reset callback.
#ifndef RP2040_PERIPHERALS_WATCHDOG_H
#define RP2040_PERIPHERALS_WATCHDOG_H

#include <array>
#include <cstdint>
#include <functional>

#include "core/bus.h"
#include "core/memory.h"

namespace rp2040 {

class Watchdog : public BusPeripheral {
public:
    static constexpr std::uint32_t kBase = 0x40058000u;
    static constexpr std::uint32_t kSize = 0x1000u;

    explicit Watchdog(std::uint32_t cycles_per_us = 125) : cycles_per_us_(cycles_per_us) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }
    void on_reset(std::function<void()> cb) { reset_cb_ = std::move(cb); }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;

    void on_cycles(std::uint64_t sys_cycles);

    std::uint32_t scratch(unsigned n) const { return scratch_[n & 0x7u]; }
    std::uint32_t reason() const { return reason_; }

private:
    void fire_reset(std::uint32_t reason_bit);

    std::uint32_t cycles_per_us_;
    std::uint64_t accum_ = 0;

    std::uint32_t load_ = 0;
    std::uint32_t counter_ = 0;
    bool enabled_ = false;
    std::uint32_t ctrl_ = 0;
    std::uint32_t reason_ = 0;
    std::uint32_t tick_ = 0;
    std::array<std::uint32_t, 8> scratch_{};
    std::function<void()> reset_cb_;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_WATCHDOG_H
