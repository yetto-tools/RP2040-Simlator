// adc.h - RP2040 ADC (datasheet 4.9): a 12-bit SAR ADC with 5 inputs
// (GPIO26-29 + the on-die temperature sensor) and a 4-entry sample FIFO.
//
// Functional model: input voltages are set by the test bench as raw 12-bit
// codes. START_ONCE converts immediately; START_MANY free-runs, paced by the
// 48 MHz ADC clock derived from on_cycles() (96 + DIV_INT clocks per sample).
#ifndef RP2040_PERIPHERALS_ADC_H
#define RP2040_PERIPHERALS_ADC_H

#include <array>
#include <cstdint>
#include <deque>

#include "core/atomic_peripheral.h"
#include "core/bus.h"
#include "core/cpu.h"
#include "core/memory.h"

namespace rp2040 {

class Adc : public AtomicPeripheral {
public:
    static constexpr std::uint32_t kBase = 0x4004C000u;
    static constexpr std::uint32_t kSize = AtomicPeripheral::kAtomicSize;
    static constexpr unsigned kNumInputs = 5;   // 4 GPIO + temperature sensor
    static constexpr unsigned kTempChannel = 4;
    static constexpr unsigned kFifoDepth = 4;
    static constexpr unsigned kIrq = kExcExternal0 + 22;  // ADC_IRQ_FIFO == IRQ22

    Adc(Cpu& cpu, std::uint32_t adc_clk_hz = 48'000'000u,
        std::uint32_t sys_clk_hz = 125'000'000u)
        : cpu_(cpu), adc_hz_(adc_clk_hz), sys_hz_(sys_clk_hz) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }

    BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth w) override;
    BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth w) override;

    // Test bench: drive an input with a raw 12-bit code (0..4095).
    void set_input(unsigned channel, std::uint16_t raw12);

    void on_cycles(std::uint64_t sys_cycles);

    std::uint16_t result() const { return result_; }
    unsigned fifo_level() const { return static_cast<unsigned>(fifo_.size()); }

private:
    void convert();          // one conversion of the selected channel
    void refresh_irq();
    unsigned next_rrobin(unsigned from) const;

    Cpu& cpu_;
    std::uint32_t adc_hz_;
    std::uint32_t sys_hz_;

    std::array<std::uint16_t, kNumInputs> input_{};
    std::deque<std::uint16_t> fifo_;
    std::uint16_t result_ = 0;

    std::uint32_t cs_ = 0;    // EN/TS_EN/START_*/AINSEL/RROBIN + READY/ERR
    std::uint32_t fcs_ = 0;   // FIFO control/status
    std::uint32_t div_ = 0;   // [23:8] int, [7:0] frac
    std::uint32_t inte_ = 0;
    std::uint32_t intf_ = 0;

    std::uint64_t adc_clk_accum_ = 0;
    std::uint32_t conv_countdown_ = 0;   // ADC clocks left in the current sample
    bool converting_ = false;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_ADC_H
