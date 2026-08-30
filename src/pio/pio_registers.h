// pio_registers.h - CPU-facing register block for one PIO instance
// (datasheet 3.7). Wraps a PioBlock: CTRL, FSTAT/FLEVEL, TXF/RXF windows,
// INSTR_MEM, the per-SM config registers and the IRQ register.
//
// PIO0 lives at 0x50200000, PIO1 at 0x50300000. The block->NVIC interrupt
// routing (INTR / IRQ0_INTE / ...) is present as state but not yet wired.
#ifndef RP2040_PIO_PIO_REGISTERS_H
#define RP2040_PIO_PIO_REGISTERS_H

#include <array>
#include <cstdint>

#include "core/bus.h"
#include "core/memory.h"
#include "pio/pio_block.h"

namespace rp2040 {

class PioRegisters : public BusPeripheral {
public:
    static constexpr std::uint32_t kPio0Base = 0x50200000u;
    static constexpr std::uint32_t kPio1Base = 0x50300000u;
    static constexpr std::uint32_t kSize = 0x1000u;

    PioRegisters(PioBlock& block, std::uint32_t base) : block_(block), base_(base) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(base_, kSize, this); }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;

private:
    void write_ctrl(std::uint32_t value);
    void write_sm_clkdiv(unsigned sm, std::uint32_t value);
    void write_sm_execctrl(unsigned sm, std::uint32_t value);
    void write_sm_shiftctrl(unsigned sm, std::uint32_t value);
    void write_sm_pinctrl(unsigned sm, std::uint32_t value);
    std::uint32_t read_fstat() const;
    std::uint32_t read_flevel() const;

    PioBlock& block_;
    std::uint32_t base_;

    std::uint32_t ctrl_ = 0;
    std::array<std::uint32_t, PioBlock::kNumSm> clkdiv_{};
    std::array<std::uint32_t, PioBlock::kNumSm> execctrl_{};
    std::array<std::uint32_t, PioBlock::kNumSm> shiftctrl_{};
    std::array<std::uint32_t, PioBlock::kNumSm> pinctrl_{};
    std::uint32_t irq0_inte_ = 0, irq0_intf_ = 0;
    std::uint32_t irq1_inte_ = 0, irq1_intf_ = 0;
};

}  // namespace rp2040

#endif  // RP2040_PIO_PIO_REGISTERS_H
