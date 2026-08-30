// cpu.h - ARMv6-M (Cortex-M0+) instruction execution (BACKLOG P1.2 tail).
//
// Ties the decoder (thumb_isa.h), the register file (registers.h) and the bus
// (memory.h) together: fetch -> decode -> execute one instruction per step().
//
// Covers the computational core (data processing, shifts, moves, compares,
// PC/SP-relative address), all branches, and the load/store instructions
// (LDR/STR family, PUSH/POP, LDM/STM).
#ifndef RP2040_CORE_CPU_H
#define RP2040_CORE_CPU_H

#include <array>
#include <cstdint>

#include "core/memory.h"
#include "core/registers.h"
#include "exceptions.h"
#include "thumb_isa.h"

namespace rp2040 {

enum class ExecStatus {
    Ok,
    Unimplemented,        // valid encoding, executor slice not written yet
    Undefined,            // internal: UNDEFINED encoding (step() -> HardFault)
    Breakpoint,           // BKPT executed (see last_bkpt_imm)
    Svc,                  // internal: SVC (step() -> SVCall)
    MemFault,             // internal: bus error (step() -> HardFault)
    WaitingForInterrupt,  // WFI / WFE
    ExceptionTaken,       // step() vectored to a handler; PC is at the handler
    Lockup,               // fault with no handler available (HardFault escalation)
};

class Cpu {
public:
    Cpu(RegisterFile& regs, Memory& mem) : regs_(regs), mem_(mem) {}

    // Architectural reset: register file reset, then MSP <- vector[0] and
    // PC <- vector[1] (via VTOR).
    void reset();

    // Fetch + decode + execute the instruction at regs.pc(), or vector a
    // pending exception if one now out-prioritises the running code.
    ExecStatus step();

    // Execute an already-decoded instruction whose first halfword is at
    // `instr_pc`. The caller must have set regs.pc() to instr_pc + d.length
    // before calling; branches overwrite it.
    ExecStatus execute(const DecodedInstr& d, std::uint32_t instr_pc);

    std::uint32_t last_svc_imm() const { return svc_imm_; }
    std::uint32_t last_bkpt_imm() const { return bkpt_imm_; }

    // Core execution cycles retired so far (Cortex-M0+ timings, zero wait
    // states). Only step() accumulates; a bare execute() does not.
    std::uint64_t cycle_count() const { return cycles_; }
    void reset_cycle_count() { cycles_ = 0; }

    // --- Exception model ------------------------------------------------
    void set_vtor(std::uint32_t v) { vtor_ = v & ~std::uint32_t{0x7F}; }
    std::uint32_t vtor() const { return vtor_; }

    // Raw 8-bit priority (only bits [7:6] are significant). `exc` in
    // [4, kMaxException]; the fixed-priority exceptions ignore this.
    void set_exception_priority(unsigned exc, std::uint8_t raw);
    std::uint8_t exception_priority(unsigned exc) const;

    void pend_exception(unsigned exc) { pending_ |= (1ull << exc); }
    void clear_pending(unsigned exc) { pending_ &= ~(1ull << exc); }
    bool is_pending(unsigned exc) const { return (pending_ & (1ull << exc)) != 0; }

    // Priority of the code currently executing (256 = base/thread, no
    // exception active); PRIMASK raises it to 0.
    int current_execution_priority() const;

    // Drive one exception entry directly (mainly for tests). `return_address`
    // is what the handler's frame will resume to.
    ExecStatus take_exception(unsigned exc, std::uint32_t return_address);

private:
    // R15 reads yield instr_pc + 4 (ARMv6-M); callers needing Align(PC,4)
    // mask the low two bits themselves.
    std::uint32_t read_reg(unsigned n, std::uint32_t instr_pc) const;
    void write_reg(unsigned n, std::uint32_t value);

    ExecStatus exec_data(const DecodedInstr& d, std::uint32_t instr_pc);
    ExecStatus exec_branch(const DecodedInstr& d, std::uint32_t instr_pc);
    ExecStatus exec_load_store(const DecodedInstr& d, std::uint32_t instr_pc);

    int priority_of(unsigned exc) const;
    int highest_pending_exception() const;   // 0 if none is eligible
    ExecStatus exception_return(std::uint32_t exc_return);

    RegisterFile& regs_;
    Memory& mem_;
    std::uint64_t cycles_ = 0;
    std::uint32_t vtor_ = 0;
    std::uint64_t pending_ = 0;   // bit e set => exception e pending
    std::array<std::uint8_t, kExceptionTableEntries> priority_{};
    std::uint32_t svc_imm_ = 0;
    std::uint32_t bkpt_imm_ = 0;
};

}  // namespace rp2040

#endif  // RP2040_CORE_CPU_H
