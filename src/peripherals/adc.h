// adc.h - RP2040 ADC (datasheet 4.9): a 12-bit SAR ADC with 5 inputs
// (GPIO26-29 + the on-die temperature sensor) and a 4-entry sample FIFO.
//
// Bit-accurate SAR timing: both START_ONCE and START_MANY take the real
// 96 + DIV_INT ADC clocks per sample (paced by on_cycles() against the
// 48 MHz ADC clock) - CS.READY stays clear for the whole conversion, not
// just an instant. Input "voltages" are set by the test bench as raw 12-bit
// codes (set_input()); there is no analog voltage model behind the GPIO
// pins to sample instead, and no DMA DREQ line (see the DMA controller's
// own DREQ approximation) - both are permanent scope limits, not TODOs.
#ifndef RP2040_PERIPHERALS_ADC_H
#define RP2040_PERIPHERALS_ADC_H

#include <array>
#include <cstdint>
#include <deque>

#include "core/atomic_peripheral.h"
#include "core/bus.h"
#include "core/cpu.h"
#include "core/interrupt_controller.h"
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
        : nvic_(cpu), adc_hz_(adc_clk_hz), sys_hz_(sys_clk_hz) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }

    BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth w) override;
    BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth w) override;
    void reset() override;

    // Test bench: drive an input with a raw 12-bit code (0..4095).
    void set_input(unsigned channel, std::uint16_t raw12);

    void on_cycles(std::uint64_t sys_cycles);

    // clk_adc and clk_sys in Hz (pushed by the clock tree).
    void set_clock_hz(std::uint32_t adc_hz, std::uint32_t sys_hz) {
        adc_hz_ = adc_hz == 0 ? 1u : adc_hz;
        sys_hz_ = sys_hz == 0 ? 1u : sys_hz;
    }

    std::uint16_t result() const { return result_; }
    unsigned fifo_level() const { return static_cast<unsigned>(fifo_.size()); }

    // Wire the second Cortex-M0+ core into this peripheral's IRQ.
    void connect_core1(Cpu* c) { nvic_.connect(c); }

    // DREQ readiness (datasheet 2.5.3.1: DREQ_ADC), gated by FCS.DREQ_EN
    // ("assert DMA requests when FIFO contains data") like real hardware.
    bool dreq_ready() const;

private:
    void convert();          // one conversion of the selected channel
    void refresh_irq();
    unsigned next_rrobin(unsigned from) const;

    InterruptController nvic_;
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
