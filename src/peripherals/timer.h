// timer.h - RP2040 TIMER (datasheet 4.6): a 64-bit microsecond counter with
// four 32-bit alarms wired to TIMER_IRQ_0..3 (NVIC IRQ 0..3).
//
// The counter ticks once per microsecond. `on_cycles()` advances it from the
// system clock: cycles_per_us system clocks == one microsecond tick.
#ifndef RP2040_PERIPHERALS_TIMER_H
#define RP2040_PERIPHERALS_TIMER_H

#include <array>
#include <cstdint>

#include "core/atomic_peripheral.h"
#include "core/bus.h"
#include "core/cpu.h"
#include "core/memory.h"

namespace rp2040 {

class Timer : public AtomicPeripheral {
public:
    static constexpr std::uint32_t kBase = 0x40054000u;
    static constexpr std::uint32_t kSize = AtomicPeripheral::kAtomicSize;
    static constexpr unsigned kNumAlarms = 4;
    static constexpr unsigned kIrq0 = kExcExternal0 + 0;  // TIMER_IRQ_0 == IRQ0

    // `cycles_per_us` system clocks make one microsecond (125 at 125 MHz).
    explicit Timer(Cpu& cpu, std::uint32_t cycles_per_us = 125);

    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }

    BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth w) override;
    BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth w) override;

    void on_cycles(std::uint64_t sys_cycles);
    std::uint64_t now_us() const { return counter_; }

private:
    void fire_due_alarms();
    void refresh_irqs();

    Cpu& cpu_;
    std::uint32_t cycles_per_us_;
    std::uint64_t accum_ = 0;
    std::uint64_t counter_ = 0;

    std::uint32_t write_staging_hi_ = 0;  // TIMEHW
    std::uint32_t read_latch_hi_ = 0;     // snapshot taken when TIMELR is read
    bool paused_ = false;

    std::array<std::uint32_t, kNumAlarms> alarm_{};
    std::uint8_t armed_ = 0;
    std::uint8_t intr_ = 0;
    std::uint8_t inte_ = 0;
    std::uint8_t intf_ = 0;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_TIMER_H
