// pio_block.h - one RP2040 PIO block: 4 state machines sharing a 32-word
// instruction memory and an 8-bit IRQ register, each with its own fractional
// clock divider (datasheet 3.2, 3.3).
#ifndef RP2040_PIO_PIO_BLOCK_H
#define RP2040_PIO_PIO_BLOCK_H

#include <array>
#include <cstdint>

#include "peripherals/gpio.h"
#include "pio/state_machine.h"
#include "rp2040.h"

namespace rp2040 {

class PioBlock {
public:
    static constexpr unsigned kNumSm = kStateMachinesPerBlock;      // 4
    static constexpr unsigned kInstrWords = kPioInstrMemWords;      // 32

    // `index` is 0 (PIO0) or 1 (PIO1); it selects which GPIO driver the SMs
    // present and is not otherwise used.
    PioBlock(Gpio& gpio, int index);

    PioBlock(const PioBlock&) = delete;
    PioBlock& operator=(const PioBlock&) = delete;

    StateMachine& sm(unsigned i) { return sm_[i]; }
    const StateMachine& sm(unsigned i) const { return sm_[i]; }

    void write_instruction(unsigned addr, std::uint16_t word) {
        if (addr < kInstrWords) program_[addr] = word;
    }
    std::uint16_t instruction(unsigned addr) const {
        return addr < kInstrWords ? program_[addr] : 0u;
    }

    // 8 IRQ flags shared by the block (and visible to the CPU / other block).
    std::uint8_t irq() const { return irq_; }
    void set_irq(unsigned n, bool v);

    // Fractional clock divider for one SM: SM clock = sys clock / (int + frac/256).
    // An integer part of 0 means 65536 (datasheet 3.5.5).
    void set_clkdiv(unsigned sm, std::uint16_t int_part, std::uint8_t frac);
    void restart_clkdiv(unsigned sm) { if (sm < kNumSm) clkacc_[sm] = 0; }

    // Advance the whole block by one system clock: each SM whose divided clock
    // ticks this cycle executes one instruction.
    void tick();

private:
    Gpio& gpio_;
    int index_;
    std::array<std::uint16_t, kInstrWords> program_{};
    std::array<StateMachine, kNumSm> sm_{};
    std::uint8_t irq_ = 0;
    std::array<std::uint32_t, kNumSm> clkdiv_{};   // int*256 + frac
    std::array<std::uint32_t, kNumSm> clkacc_{};
};

}  // namespace rp2040

#endif  // RP2040_PIO_PIO_BLOCK_H
