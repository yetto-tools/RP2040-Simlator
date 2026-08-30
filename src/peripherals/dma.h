// dma.h - RP2040 DMA controller (datasheet 2.5): 12 channels at 0x50000000.
//
// Functional model: a channel trigger runs the whole transfer immediately
// through the Memory bus (byte/half/word, address increment, ring wrap,
// byte-swap), then chains and raises its interrupt. DREQ pacing is not
// modelled - every TREQ_SEL is treated as "unpaced".
#ifndef RP2040_PERIPHERALS_DMA_H
#define RP2040_PERIPHERALS_DMA_H

#include <array>
#include <cstdint>

#include "core/atomic_peripheral.h"
#include "core/bus.h"
#include "core/cpu.h"
#include "core/interrupt_controller.h"
#include "core/memory.h"

namespace rp2040 {

class Dma : public AtomicPeripheral {
public:
    static constexpr std::uint32_t kBase = 0x50000000u;
    static constexpr std::uint32_t kSize = AtomicPeripheral::kAtomicSize;
    static constexpr unsigned kNumChannels = 12;
    static constexpr unsigned kIrq0 = kExcExternal0 + 11;  // DMA_IRQ_0 == IRQ11
    static constexpr unsigned kIrq1 = kExcExternal0 + 12;

    explicit Dma(Cpu& cpu, Memory& mem) : nvic_(cpu), mem_(mem) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }

    BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth w) override;
    BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth w) override;

    // Inspection for tests.
    std::uint32_t trans_count(unsigned ch) const { return chan_[ch].trans_count; }
    bool channel_busy(unsigned ch) const { return (chan_[ch].ctrl & (1u << 24)) != 0; }
    std::uint32_t intr() const { return intr_; }

    // Wire the second Cortex-M0+ core into this peripheral's IRQ.
    void connect_core1(Cpu* c) { nvic_.connect(c); }

private:
    struct Channel {
        std::uint32_t read_addr = 0;
        std::uint32_t write_addr = 0;
        std::uint32_t trans_count = 0;
        std::uint32_t ctrl = 0;
    };

    void trigger(unsigned ch);
    void run_transfer(unsigned ch);
    void refresh_irqs();

    InterruptController nvic_;
    Memory& mem_;
    std::array<Channel, kNumChannels> chan_{};
    std::uint32_t intr_ = 0;                  // per-channel raw interrupt
    std::array<std::uint32_t, 2> inte_{};
    std::array<std::uint32_t, 2> intf_{};
    unsigned chain_depth_ = 0;                // guard against self/loop chains
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_DMA_H
