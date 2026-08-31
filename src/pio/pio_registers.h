// pio_registers.h - CPU-facing register block for one PIO instance
// (datasheet 3.7). Wraps a PioBlock: CTRL, FSTAT/FLEVEL, TXF/RXF windows,
// INSTR_MEM, the per-SM config registers and the IRQ register.
//
// PIO0 lives at 0x50200000, PIO1 at 0x50300000. The block->NVIC interrupt
// routing (INTR / IRQ0_INTE / ...) is wired via connect_nvic()/poll_interrupts().
#ifndef RP2040_PIO_PIO_REGISTERS_H
#define RP2040_PIO_PIO_REGISTERS_H

#include <array>
#include <cstdint>

#include "core/atomic_peripheral.h"
#include "core/bus.h"
#include "core/cpu.h"
#include "core/interrupt_controller.h"
#include "core/memory.h"
#include "pio/pio_block.h"

namespace rp2040 {

class PioRegisters : public AtomicPeripheral {
public:
    static constexpr std::uint32_t kPio0Base = 0x50200000u;
    static constexpr std::uint32_t kPio1Base = 0x50300000u;
    static constexpr std::uint32_t kSize = AtomicPeripheral::kAtomicSize;
    // PIO0_IRQ_0 = IRQ7, PIO0_IRQ_1 = IRQ8, PIO1_IRQ_0 = IRQ9, PIO1_IRQ_1 = IRQ10.
    static constexpr unsigned kPio0Irq0 = kExcExternal0 + 7;
    static constexpr unsigned kPio1Irq0 = kExcExternal0 + 9;

    PioRegisters(PioBlock& block, std::uint32_t base) : block_(block), base_(base) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(base_, kSize, this); }

    // Route the block's interrupt state onto the NVIC. `cpu` is borrowed;
    // `irq0` is the exception number for this instance's IRQ_0 line (IRQ_1 is
    // irq0 + 1). Call poll_interrupts() after advancing the block.
    void connect_nvic(Cpu* cpu, unsigned irq0) { nvic_.connect(cpu); nvic_irq0_ = irq0; }
    void connect_core1(Cpu* core1) { nvic_.connect(core1); }
    void poll_interrupts();

    // Latch this cycle's per-SM RXSTALL/TXSTALL into the sticky FDEBUG bits.
    // Call once per system clock, after PioBlock::tick() (datasheet 3.5.4);
    // unlike poll_interrupts() this cannot be deferred to the end of a
    // multi-cycle CPU instruction, since a stall on an intermediate cycle
    // would otherwise be lost when PioBlock overwrites its last_outcome().
    void poll_fdebug();

    BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth w) override;
    BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth w) override;

private:
    void write_ctrl(std::uint32_t value);
    void write_sm_clkdiv(unsigned sm, std::uint32_t value);
    void write_sm_execctrl(unsigned sm, std::uint32_t value);
    void write_sm_shiftctrl(unsigned sm, std::uint32_t value);
    void write_sm_pinctrl(unsigned sm, std::uint32_t value);
    std::uint32_t read_fstat() const;
    std::uint32_t read_flevel() const;
    std::uint32_t compute_intr() const;   // the 12 raw interrupt-source bits

    PioBlock& block_;
    std::uint32_t base_;
    InterruptController nvic_;
    unsigned nvic_irq0_ = 0;

    std::uint32_t ctrl_ = 0;
    std::array<std::uint32_t, PioBlock::kNumSm> clkdiv_{};
    std::array<std::uint32_t, PioBlock::kNumSm> execctrl_{};
    std::array<std::uint32_t, PioBlock::kNumSm> shiftctrl_{};
    std::array<std::uint32_t, PioBlock::kNumSm> pinctrl_{};
    std::uint32_t irq0_inte_ = 0, irq0_intf_ = 0;
    std::uint32_t irq1_inte_ = 0, irq1_intf_ = 0;
    std::uint32_t fdebug_ = 0;  // sticky, write-1-clear (datasheet 3.5.4)
};

}  // namespace rp2040

#endif  // RP2040_PIO_PIO_REGISTERS_H
