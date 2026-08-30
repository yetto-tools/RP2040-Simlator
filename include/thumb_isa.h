// thumb_isa.h - ARMv6-M Thumb instruction set: mnemonics and decoded form.
//
// The Cortex-M0+ implements ARMv6-M: the full set of 16-bit Thumb encodings
// plus six 32-bit ones (BL, MSR, MRS, DSB, DMB, ISB). There are no IT blocks,
// no CBZ/CBNZ, no LDRD/STRD, no 32-bit data processing.
//
// Reference: ARMv6-M Architecture Reference Manual (DDI 0419), chapter A5
// ("Thumb Instruction Set Encoding") and A6 ("Instruction Details").
#ifndef RP2040_THUMB_ISA_H
#define RP2040_THUMB_ISA_H

#include <cstdint>

namespace rp2040 {

enum class Mnemonic : std::uint8_t {
    UNDEFINED,   // not a valid ARMv6-M encoding (UNDEFINED -> HardFault)
    UNPREDICTABLE,

    // A5.2.1 Shift (imm), add, subtract, move, compare
    LSL_imm, LSR_imm, ASR_imm,
    ADD_reg, SUB_reg,        // T1: 3 low registers
    ADD_imm3, SUB_imm3,
    MOV_imm, CMP_imm, ADD_imm8, SUB_imm8,

    // A5.2.2 Data processing (all set flags; two low registers)
    AND_reg, EOR_reg, LSL_reg, LSR_reg, ASR_reg, ADC_reg, SBC_reg, ROR_reg,
    TST_reg, RSB_imm, CMP_reg, CMN_reg, ORR_reg, MUL, BIC_reg, MVN_reg,

    MOV_reg,     // MOVS Rd,Rm  (== LSLS Rd,Rm,#0, encoding T2) - sets flags

    // A5.2.3 Special data instructions and branch and exchange
    ADD_reg_hi, CMP_reg_hi, MOV_reg_hi, BX, BLX_reg,

    // Load/store
    LDR_lit,                                   // A6.7.43 (LDR literal)
    STR_reg, STRH_reg, STRB_reg, LDRSB_reg,
    LDR_reg, LDRH_reg, LDRB_reg, LDRSH_reg,
    STR_imm, LDR_imm, STRB_imm, LDRB_imm, STRH_imm, LDRH_imm,
    STR_imm_sp, LDR_imm_sp,

    // PC/SP relative address
    ADR,             // ADD (PC plus imm), Rd = Align(PC,4) + imm
    ADD_SP_imm,      // T1: Rd = SP + imm
    ADD_SP_sp_imm,   // T2: SP = SP + imm
    SUB_SP_imm,      // SP = SP - imm

    // A5.2.5 Miscellaneous 16-bit
    SXTH, SXTB, UXTH, UXTB,
    PUSH, POP,
    REV, REV16, REVSH,
    CPS,             // CPSIE/CPSID (imm bit0 = disable)
    BKPT,
    NOP, YIELD, WFE, WFI, SEV,   // hints

    // Load/store multiple
    STM, LDM,

    // Branches
    B_cond,          // A6.7.10 B (T1), conditional
    SVC,             // A6.7.117
    UDF,             // A6.7.126 (permanently undefined)
    B,               // A6.7.10 B (T2), unconditional

    // 32-bit
    BL,              // A6.7.13
    MSR, MRS,        // A6.7.83 / A6.7.82
    DSB, DMB, ISB,   // A6.7.28 / A6.7.26 / A6.7.38
};

// One decoded instruction. Fields carry meaning only for the mnemonics that
// use them; the rest stay at their defaults.
struct DecodedInstr {
    Mnemonic op = Mnemonic::UNDEFINED;
    std::uint8_t length = 2;              // instruction size in bytes (2 or 4)

    std::uint8_t rd = 0;
    std::uint8_t rn = 0;
    std::uint8_t rm = 0;
    std::uint8_t rt = 0;

    // Immediate, already zero-extended and scaled to its final byte/word value
    // where the encoding fixes that (e.g. LDR(imm) T1 stores imm5 << 2).
    std::uint32_t imm = 0;

    // Signed byte offset for B / B_cond / BL, relative to the instruction's
    // PC-read value. The execute stage adds it to (PC of instr + 4).
    std::int32_t branch_offset = 0;

    std::uint8_t cond = 0xE;              // condition for B_cond (0..14; 0xE = AL)
    std::uint16_t register_list = 0;      // bit i set => Ri, for PUSH/POP/STM/LDM

    bool setflags = false;                // updates APSR.{N,Z,C,V}
    bool add = true;                      // offset/adjust direction (address, SP)
    bool wback = false;                   // base register writeback (LDM/STM)

    std::uint32_t raw = 0;                // the raw 16- or 32-bit encoding
};

// True if a 16-bit halfword `hw1` begins a 32-bit Thumb instruction
// (ARMv6-M ARM A5.1: top 5 bits are 0b11101, 0b11110 or 0b11111).
inline bool is_32bit_thumb(std::uint16_t hw1) {
    const std::uint16_t top5 = static_cast<std::uint16_t>(hw1 >> 11);
    return top5 == 0b11101 || top5 == 0b11110 || top5 == 0b11111;
}

// Decode a 16-bit Thumb instruction.
DecodedInstr decode_thumb16(std::uint16_t instr);

// Decode a 32-bit Thumb instruction from its two halfwords.
DecodedInstr decode_thumb32(std::uint16_t hw1, std::uint16_t hw2);

// Human-readable mnemonic name (for traces / disassembly / test messages).
const char* to_string(Mnemonic m);

}  // namespace rp2040

#endif  // RP2040_THUMB_ISA_H
