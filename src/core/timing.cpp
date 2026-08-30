#include "core/timing.h"

namespace rp2040 {

unsigned instruction_cycles(const DecodedInstr& d, bool took_branch, unsigned reg_count) {
    switch (d.op) {
        // --- single load/store: 2 cycles ---------------------------------
        case Mnemonic::LDR_lit:
        case Mnemonic::LDR_reg:  case Mnemonic::LDR_imm:  case Mnemonic::LDR_imm_sp:
        case Mnemonic::LDRB_reg: case Mnemonic::LDRB_imm:
        case Mnemonic::LDRH_reg: case Mnemonic::LDRH_imm:
        case Mnemonic::LDRSB_reg: case Mnemonic::LDRSH_reg:
        case Mnemonic::STR_reg:  case Mnemonic::STR_imm:  case Mnemonic::STR_imm_sp:
        case Mnemonic::STRB_reg: case Mnemonic::STRB_imm:
        case Mnemonic::STRH_reg: case Mnemonic::STRH_imm:
            return 2;

        // --- multi-register transfer: 1 + N -----------------------------
        case Mnemonic::LDM:
        case Mnemonic::STM:
        case Mnemonic::PUSH:
            return 1 + reg_count;
        case Mnemonic::POP:
            // POP with PC costs an extra pipeline reload.
            return ((d.register_list & (1u << 15)) != 0 ? 4u : 1u) + reg_count;

        // --- branches ------------------------------------------------
        case Mnemonic::B:
            return 3;
        case Mnemonic::B_cond:
            return took_branch ? 3u : 1u;
        case Mnemonic::BL:
            return 4;
        case Mnemonic::BX:
        case Mnemonic::BLX_reg:
            return 3;

        // --- ALU/MOV writing PC forces a pipeline reload ---------------
        case Mnemonic::ADD_reg_hi:
        case Mnemonic::MOV_reg_hi:
            return d.rd == 15 ? 3u : 1u;

        // --- special --------------------------------------------------
        case Mnemonic::MRS:
        case Mnemonic::MSR:
            return 4;
        case Mnemonic::DSB:
        case Mnemonic::DMB:
        case Mnemonic::ISB:
            return 3;
        case Mnemonic::WFI:
        case Mnemonic::WFE:
            return 2;

        // MUL is single-cycle on the RP2040's Cortex-M0+ (fast multiplier).
        case Mnemonic::MUL:
            return 1;

        // Everything else (data processing, shifts, moves, CMP, extends,
        // REV, ADR, ADD/SUB SP, NOP-family, CPS): 1 cycle.
        default:
            return 1;
    }
}

}  // namespace rp2040
