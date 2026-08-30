// pwm.h - RP2040 PWM (datasheet 4.5): 8 slices, each with two channels (A, B)
// and a 16-bit counter. Channel A/B of slice N drive GPIO 2N / 2N+1 (and
// +16) when their FUNCSEL is PWM.
//
// Modelled: free-running divider (DIVMODE 0), count-up and phase-correct
// count-up/down, TOP wrap, CC compare -> GPIO level, per-slice wrap IRQ.
// Not modelled: B-pin gated/edge divider modes, phase advance/retard.
#ifndef RP2040_PERIPHERALS_PWM_H
#define RP2040_PERIPHERALS_PWM_H

#include <array>
#include <cstdint>

#include "core/atomic_peripheral.h"
#include "core/bus.h"
#include "core/cpu.h"
#include "core/memory.h"
#include "peripherals/gpio.h"

namespace rp2040 {

class Pwm : public AtomicPeripheral {
public:
    static constexpr std::uint32_t kBase = 0x40050000u;
    static constexpr std::uint32_t kSize = AtomicPeripheral::kAtomicSize;
    static constexpr unsigned kNumSlices = 8;
    static constexpr unsigned kIrqWrap = kExcExternal0 + 4;  // PWM_IRQ_WRAP == IRQ4

    Pwm(Cpu& cpu, Gpio& gpio) : cpu_(cpu), gpio_(gpio) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }

    BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth w) override;
    BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth w) override;

    void on_cycles(std::uint64_t sys_cycles);

    std::uint16_t counter(unsigned slice) const { return slice_[slice].ctr; }

private:
    struct Slice {
        std::uint32_t csr = 0;
        std::uint32_t div = 0x010;   // reset: integer divide by 1 (INT in [11:4])
        std::uint16_t ctr = 0;
        std::uint16_t cc_a = 0;
        std::uint16_t cc_b = 0;
        std::uint16_t top = 0xFFFF;
        std::uint32_t frac_accum = 0;  // 8-bit fractional divider accumulator
        bool counting_down = false;
    };

    void advance_slice(unsigned s);
    void update_outputs(unsigned s);
    void refresh_irq();

    Cpu& cpu_;
    Gpio& gpio_;
    std::array<Slice, kNumSlices> slice_{};
    std::uint32_t enable_ = 0;   // global EN register, one bit per slice
    std::uint32_t intr_ = 0;
    std::uint32_t inte_ = 0;
    std::uint32_t intf_ = 0;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_PWM_H
