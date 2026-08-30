// dma.h - RP2040 DMA controller (datasheet 2.5): 12 channels at 0x50000000.
//
// A channel trigger arms the transfer; the data is moved by on_cycles(), one
// or more elements per system clock according to the channel's DREQ pacing:
//
//   TREQ_SEL == 0x3f (PERMANENT)    -> one element per clock (unpaced)
//   TREQ_SEL 0x3b..0x3e (TIMER0..3) -> rate = sys_clk * X / Y from the
//                                     matching DMA pacing timer register
//   TREQ_SEL 0x00..0x3a (a DREQ)    -> approximated as one element every
//                                     dreq_divisor() clocks (a full FIFO-level
//                                     handshake is not modelled)
//
// Byte/half/word size, address increment, ring wrap, byte-swap, CHAIN_TO,
// MULTI_CHAN_TRIGGER, CHAN_ABORT and the INTR/INTE/INTF/INTS -> DMA_IRQ_0/1
// paths are all modelled.
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

    // Advance every armed channel by `sys_cycles` system clocks.
    void on_cycles(std::uint64_t sys_cycles);

    BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth w) override;
    BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth w) override;

    // Approximate pacing for a peripheral DREQ (clocks per element). Default 2.
    void set_dreq_divisor(std::uint32_t clocks) { dreq_divisor_ = clocks == 0 ? 1u : clocks; }

    // Wire the second Cortex-M0+ core into this peripheral's IRQ.
    void connect_core1(Cpu* c) { nvic_.connect(c); }

    // Inspection for tests.
    std::uint32_t trans_count(unsigned ch) const { return chan_[ch].trans_count; }
    std::uint32_t remaining(unsigned ch) const { return chan_[ch].remaining; }
    bool channel_busy(unsigned ch) const { return (chan_[ch].ctrl & (1u << 24)) != 0; }
    std::uint32_t intr() const { return intr_; }

private:
    struct Channel {
        std::uint32_t read_addr = 0;
        std::uint32_t write_addr = 0;
        std::uint32_t trans_count = 0;   // last programmed count (reads 0 when done)
        std::uint32_t ctrl = 0;
        std::uint32_t remaining = 0;     // elements left in the running transfer
        std::uint64_t pace_accum = 0;    // fractional DREQ pacing accumulator
    };

    struct Rate { std::uint64_t num = 0; std::uint64_t den = 1; };

    void trigger(unsigned ch);
    void complete(unsigned ch, bool error);
    bool transfer_one(unsigned ch);     // false on a bus error
    Rate rate_for(const Channel& c) const;
    void refresh_irqs();

    InterruptController nvic_;
    Memory& mem_;
    std::array<Channel, kNumChannels> chan_{};
    std::uint32_t intr_ = 0;                  // per-channel raw interrupt
    std::array<std::uint32_t, 2> inte_{};
    std::array<std::uint32_t, 2> intf_{};
    std::array<std::uint32_t, 4> pacing_timer_{};  // DMA TIMER0..3 (X<<16 | Y)
    std::uint32_t dreq_divisor_ = 2;
    unsigned chain_depth_ = 0;                // guard against 0-length chain loops
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_DMA_H
