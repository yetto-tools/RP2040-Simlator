#include "debuggers/pio_debugger.h"

#include "pio/pio_disasm.h"

namespace rp2040 {

void PioDebugger::step() {
    std::uint8_t pc_before[2][PioBlock::kNumSm];
    for (unsigned b = 0; b < 2; ++b) {
        for (unsigned i = 0; i < PioBlock::kNumSm; ++i) {
            pc_before[b][i] = blocks_[b]->sm(i).pc;
        }
    }

    for (unsigned b = 0; b < 2; ++b) blocks_[b]->tick();
    ++cycle_;

    if (trace_on_) {
        for (unsigned b = 0; b < 2; ++b) {
            for (unsigned i = 0; i < PioBlock::kNumSm; ++i) {
                if (blocks_[b]->last_outcome(i).executed) {
                    const std::uint8_t pc = pc_before[b][i];
                    trace_.push_back({cycle_, b, i, pc, blocks_[b]->instruction(pc)});
                }
            }
        }
    }
}

bool PioDebugger::at_breakpoint(Hit& out) const {
    for (unsigned b = 0; b < 2; ++b) {
        for (unsigned i = 0; i < PioBlock::kNumSm; ++i) {
            if (!blocks_[b]->sm(i).enabled()) continue;
            if (bp_.count(key(b, i, blocks_[b]->sm(i).pc)) != 0) {
                out = {true, b, i, blocks_[b]->sm(i).pc};
                return true;
            }
        }
    }
    return false;
}

PioDebugger::Hit PioDebugger::run(std::uint64_t max_cycles) {
    Hit h;
    for (std::uint64_t c = 0; c < max_cycles; ++c) {
        step();
        if (at_breakpoint(h)) return h;
    }
    return h;
}

PioDebugger::SmSnapshot PioDebugger::inspect(unsigned block, unsigned sm) const {
    SmSnapshot s;
    if (block > 1 || sm >= PioBlock::kNumSm) return s;
    const PioBlock& blk = *blocks_[block];
    const StateMachine& m = blk.sm(sm);

    s.enabled = m.enabled();
    s.pc = m.pc;
    s.x = m.x;
    s.y = m.y;
    s.osr = m.osr;
    s.isr = m.isr;
    s.osr_shift_count = m.osr_shift_count;
    s.isr_shift_count = m.isr_shift_count;
    s.tx_level = m.tx.level();
    s.rx_level = m.rx.level();
    s.stalled = blk.last_outcome(sm).stalled;
    s.instructions_retired = blk.instructions_retired(sm);
    s.next_instr = m.current_instruction();
    s.disasm = pio_disassemble(s.next_instr, m.cfg.sideset_count, m.cfg.sideset_opt);
    return s;
}

}  // namespace rp2040
