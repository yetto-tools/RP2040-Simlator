// cpu.h - ARMv6-M (Cortex-M0+) instruction execution (BACKLOG P1.2 tail).
//
// Ties the decoder (thumb_isa.h), the register file (registers.h) and the bus
// (memory.h) together: fetch -> decode -> execute one instruction per step().
//
// This unit currently covers the computational core (data processing,
// shifts, moves, compares, PC/SP-relative address, and all branches).
// Memory-access instructions (LDR/STR/PUSH/POP/LDM/STM) report
// ExecStatus::Unimplemented until the next slice lands.
#ifndef RP2040_CORE_CPU_H
#define RP2040_CORE_CPU_H

#include <cstdint>

#include "core/memory.h"
#include "core/registers.h"
#include "thumb_isa.h"

namespace rp2040 {

enum class ExecStatus {
    Ok,
    Unimplemented,        // valid encoding, executor slice not written yet
    Undefined,            // UNDEFINED encoding -> HardFault
    Breakpoint,           // BKPT executed (see last_bkpt_imm)
    Svc,                  // SVC executed (see last_svc_imm)
    MemFault,             // bus error on fetch or load/store -> HardFault
    WaitingForInterrupt,  // WFI / WFE
};

class Cpu {
public:
    Cpu(RegisterFile& regs, Memory& mem) : regs_(regs), mem_(mem) {}

    // Fetch + decode + execute the instruction at regs.pc(). Advances PC.
    ExecStatus step();

    // Execute an already-decoded instruction whose first halfword is at
    // `instr_pc`. The caller must have set regs.pc() to instr_pc + d.length
    // before calling; branches overwrite it.
    ExecStatus execute(const DecodedInstr& d, std::uint32_t instr_pc);

    std::uint32_t last_svc_imm() const { return svc_imm_; }
    std::uint32_t last_bkpt_imm() const { return bkpt_imm_; }

private:
    // R15 reads yield instr_pc + 4 (ARMv6-M); callers needing Align(PC,4)
    // mask the low two bits themselves.
    std::uint32_t read_reg(unsigned n, std::uint32_t instr_pc) const;
    void write_reg(unsigned n, std::uint32_t value);

    ExecStatus exec_data(const DecodedInstr& d, std::uint32_t instr_pc);
    ExecStatus exec_branch(const DecodedInstr& d, std::uint32_t instr_pc);

    RegisterFile& regs_;
    Memory& mem_;
    std::uint32_t svc_imm_ = 0;
    std::uint32_t bkpt_imm_ = 0;
};

}  // namespace rp2040

#endif  // RP2040_CORE_CPU_H
