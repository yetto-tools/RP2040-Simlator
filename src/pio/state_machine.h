// state_machine.h - one RP2040 PIO state machine (datasheet 3.2, 3.4, 3.5).
//
// A state machine is an independent co-processor: PC, two scratch registers
// (X, Y), an output shift register (OSR) and an input shift register (ISR),
// each with a shift counter, plus a TX and RX FIFO. It executes one PIO
// instruction per post-divider clock; tick() advances it by one such clock.
//
// This slice implements the datapath: JMP (all conditions), MOV, SET, IN,
// OUT, PUSH, PULL, and autopush/autopull, with instruction wrapping and the
// delay field. WAIT, IRQ, side-set and GPIO pin effects arrive in the next
// slice (they need the block-level IRQ register and the GPIO model).
#ifndef RP2040_PIO_STATE_MACHINE_H
#define RP2040_PIO_STATE_MACHINE_H

#include <cstdint>

#include "peripherals/gpio.h"
#include "pio_fifo.h"
#include "pio_isa.h"
#include "rp2040.h"

namespace rp2040 {

struct SmConfig {
    // SHIFTCTRL
    bool out_shiftdir_right = true;   // 1 = shift OUT to the right (LSB first)
    bool in_shiftdir_right = true;
    bool autopull = false;
    bool autopush = false;
    std::uint8_t pull_threshold = 32; // 1..32 (register value 0 means 32)
    std::uint8_t push_threshold = 32;

    // EXECCTRL
    std::uint8_t wrap_top = kPioInstrMemWords - 1;  // 31
    std::uint8_t wrap_bottom = 0;
    std::uint8_t sideset_count = 0;   // bits of the delay/side-set field used for side-set
    bool sideset_opt = false;         // side-set is optional (extra enable bit)
    bool sideset_pindir = false;      // side-set targets PINDIRS instead of PINS
    std::uint8_t jmp_pin = 0;

    // PINCTRL: base pin + count for each pin group (pins wrap modulo 30).
    std::uint8_t in_base = 0;
    std::uint8_t out_base = 0;
    std::uint8_t out_count = 0;
    std::uint8_t set_base = 0;
    std::uint8_t set_count = 0;
    std::uint8_t sideset_base = 0;
};

class StateMachine {
public:
    struct TickOutcome {
        bool executed = false;  // an instruction retired this tick
        bool stalled = false;   // no forward progress (waiting on a FIFO, etc.)
        bool delayed = false;   // idle in an instruction's delay slot
    };

    // Shared 32-word instruction memory of the owning PIO block.
    void set_program(const std::uint16_t* prog) { program_ = prog; }

    // Wiring provided by the owning PIO block.
    void set_gpio(Gpio* g, Gpio::Driver drv) { gpio_ = g; driver_ = drv; }
    void set_block_irq(std::uint8_t* irq_reg) { block_irq_ = irq_reg; }
    void set_sm_id(unsigned id) { sm_id_ = id; }

    void set_enabled(bool en) { enabled_ = en; }
    bool enabled() const { return enabled_; }

    // Reset the SM datapath (as PIO CTRL SM_RESTART does): PC to wrap_bottom,
    // shift counters cleared, OSR/ISR emptied, delay/stall cleared. Scratch X/Y
    // and the FIFOs are left alone (matching hardware).
    void restart();

    // Advance by one post-divider clock.
    TickOutcome tick();

    // Execute one instruction out of band (PIO SMx_INSTR write / OUT EXEC).
    // Best effort: a stalling instruction is not retried here.
    void exec_immediate(std::uint16_t word);

    // The instruction word the SM would fetch next (SMx_INSTR read).
    std::uint16_t current_instruction() const {
        return program_ != nullptr ? program_[pc & 0x1Fu] : 0u;
    }

    // --- state (public for the register block and tests) ---
    SmConfig cfg;
    PioFifo tx;
    PioFifo rx;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t osr = 0;
    std::uint32_t isr = 0;
    std::uint8_t pc = 0;
    std::uint32_t osr_shift_count = 32;  // bits shifted out since last refill (>= thresh => empty)
    std::uint32_t isr_shift_count = 0;   // bits shifted in since last push (>= thresh => full)

private:
    enum class Stall : std::uint8_t { None, Instr, AutoPush, AutoPull };

    unsigned pull_thresh() const { return cfg.pull_threshold == 0 ? 32u : cfg.pull_threshold; }
    unsigned push_thresh() const { return cfg.push_threshold == 0 ? 32u : cfg.push_threshold; }
    unsigned delay_of(const PioInstr& in) const;

    bool exec(const PioInstr& in);   // false => stalled (stall_ says why)
    void advance_pc(const PioInstr& in);
    bool do_autopull();              // returns true if OSR is usable afterwards
    void maybe_autopush();
    void apply_sideset(const PioInstr& in);
    std::uint32_t read_pins(std::uint8_t base, unsigned count) const;
    void write_pins(std::uint8_t base, unsigned count, std::uint32_t value, bool dirs);
    unsigned resolve_irq(std::uint8_t index) const;

    const std::uint16_t* program_ = nullptr;
    Gpio* gpio_ = nullptr;
    Gpio::Driver driver_ = Gpio::kPio0;
    std::uint8_t* block_irq_ = nullptr;
    unsigned sm_id_ = 0;
    bool enabled_ = false;
    unsigned delay_left_ = 0;
    Stall stall_ = Stall::None;
    bool irq_wait_raised_ = false;
    PioInstr cur_{};
};

}  // namespace rp2040

#endif  // RP2040_PIO_STATE_MACHINE_H
