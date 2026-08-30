// pio_debugger.h - inspection and breakpoint control for the two PIO blocks
// (BACKLOG P7.5).
//
// Wraps the PioBlock pair with: per-state-machine breakpoints (stop when an
// SM is about to execute a given program address), single-clock stepping and
// run-until-break, a register/FIFO snapshot per SM, an optional instruction
// trace, and per-SM retired-instruction counts.
#ifndef RP2040_DEBUGGERS_PIO_DEBUGGER_H
#define RP2040_DEBUGGERS_PIO_DEBUGGER_H

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "pio/pio_block.h"

namespace rp2040 {

class PioDebugger {
public:
    PioDebugger(PioBlock& block0, PioBlock& block1) : blocks_{&block0, &block1} {}

    // --- breakpoints -------------------------------------------------
    void add_breakpoint(unsigned block, unsigned sm, unsigned pc) {
        bp_.insert(key(block, sm, pc));
    }
    void remove_breakpoint(unsigned block, unsigned sm, unsigned pc) {
        bp_.erase(key(block, sm, pc));
    }
    void clear_breakpoints() { bp_.clear(); }
    bool has_breakpoint(unsigned block, unsigned sm, unsigned pc) const {
        return bp_.count(key(block, sm, pc)) != 0;
    }

    // --- execution control -----------------------------------------
    struct Hit {
        bool hit = false;
        unsigned block = 0;
        unsigned sm = 0;
        unsigned pc = 0;
    };

    // Advance both blocks by one system clock.
    void step();

    // Step until an enabled SM is about to execute a breakpoint address or
    // `max_cycles` clocks have elapsed.
    Hit run(std::uint64_t max_cycles);

    std::uint64_t cycle() const { return cycle_; }

    // --- inspection ----------------------------------------------
    struct SmSnapshot {
        bool enabled = false;
        std::uint8_t pc = 0;
        std::uint32_t x = 0, y = 0, osr = 0, isr = 0;
        std::uint32_t osr_shift_count = 0, isr_shift_count = 0;
        unsigned tx_level = 0, rx_level = 0;
        bool stalled = false;
        std::uint64_t instructions_retired = 0;
        std::uint16_t next_instr = 0;
        std::string disasm;
    };
    SmSnapshot inspect(unsigned block, unsigned sm) const;

    // --- instruction trace ---------------------------------------
    struct TraceEntry {
        std::uint64_t cycle;
        unsigned block;
        unsigned sm;
        std::uint8_t pc;
        std::uint16_t instr;
    };
    void set_trace(bool on) { trace_on_ = on; }
    void clear_trace() { trace_.clear(); }
    const std::vector<TraceEntry>& trace() const { return trace_; }

private:
    static std::uint32_t key(unsigned block, unsigned sm, unsigned pc) {
        return (block << 16) | (sm << 8) | (pc & 0xFF);
    }
    bool at_breakpoint(Hit& out) const;

    PioBlock* blocks_[2];
    std::set<std::uint32_t> bp_;
    std::vector<TraceEntry> trace_;
    std::uint64_t cycle_ = 0;
    bool trace_on_ = false;
};

}  // namespace rp2040

#endif  // RP2040_DEBUGGERS_PIO_DEBUGGER_H
