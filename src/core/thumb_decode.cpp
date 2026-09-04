// thumb_decode.cpp - ARMv6-M Thumb instruction decoder (BACKLOG P1.2).
//
// Pure function: (halfword[s]) -> DecodedInstr. No CPU/memory state touched.
// Every encoding table reference is to the ARMv6-M ARM (DDI 0419), chapter A5.
#include "thumb_isa.h"

namespace rp2040 {

namespace {

// Extract bits [hi:lo] of a 16-bit value (field width must be <= 8).
constexpr std::uint8_t bits(std::uint16_t v, unsigned hi, unsigned lo) {
    return static_cast<std::uint8_t>((v >> lo) & ((1u << (hi - lo + 1)) - 1u));
}
constexpr std::uint8_t bit(std::uint16_t v, unsigned n) {
    return static_cast<std::uint8_t>((v >> n) & 1u);
}

// Sign-extend the low `width` bits of `v` to a 32-bit signed value.
constexpr std::int32_t sign_extend(std::uint32_t v, unsigned width) {
    const std::uint32_t m = std::uint32_t{1} << (width - 1);
    return static_cast<std::int32_t>((v ^ m) - m);
}

DecodedInstr undef(std::uint32_t raw, std::uint8_t len = 2) {
    DecodedInstr d;
    d.op = Mnemonic::UNDEFINED;
    d.length = len;
    d.raw = raw;
    return d;
}

// --- A5.2.1 Shift (immediate), add, subtract, move, compare ---------------
DecodedInstr decode_shift_addsub_movcmp(std::uint16_t instr) {
    DecodedInstr d;
    d.raw = instr;
    d.setflags = true;  // ARMv6-M: no IT, these 16-bit forms always set flags

    switch (bits(instr, 15, 11)) {
        case 0b00000: {  // LSL (imm)  -- imm5 == 0 means MOV (register) T2
            const std::uint8_t imm5 = bits(instr, 10, 6);
            d.rd = bits(instr, 2, 0);
            d.rm = bits(instr, 5, 3);
            d.imm = imm5;
            d.op = (imm5 == 0) ? Mnemonic::MOV_reg : Mnemonic::LSL_imm;
            return d;
        }
        case 0b00001:  // LSR (imm)
            d.op = Mnemonic::LSR_imm;
            d.rd = bits(instr, 2, 0); d.rm = bits(instr, 5, 3); d.imm = bits(instr, 10, 6);
            return d;
        case 0b00010:  // ASR (imm)
            d.op = Mnemonic::ASR_imm;
            d.rd = bits(instr, 2, 0); d.rm = bits(instr, 5, 3); d.imm = bits(instr, 10, 6);
            return d;
        case 0b00011: {  // ADD/SUB register or 3-bit immediate
            d.rd = bits(instr, 2, 0);
            d.rn = bits(instr, 5, 3);
            switch (bits(instr, 10, 9)) {
                case 0b00: d.op = Mnemonic::ADD_reg; d.rm = bits(instr, 8, 6); break;
                case 0b01: d.op = Mnemonic::SUB_reg; d.rm = bits(instr, 8, 6); break;
                case 0b10: d.op = Mnemonic::ADD_imm3; d.imm = bits(instr, 8, 6); break;
                default:   d.op = Mnemonic::SUB_imm3; d.imm = bits(instr, 8, 6); break;
            }
            return d;
        }
        case 0b00100:  // MOV (imm)
            d.op = Mnemonic::MOV_imm;
            d.rd = bits(instr, 10, 8); d.imm = bits(instr, 7, 0);
            return d;
        case 0b00101:  // CMP (imm)
            d.op = Mnemonic::CMP_imm;
            d.rn = bits(instr, 10, 8); d.imm = bits(instr, 7, 0);
            return d;
        case 0b00110:  // ADD (imm) T2
            d.op = Mnemonic::ADD_imm8;
            d.rd = d.rn = bits(instr, 10, 8); d.imm = bits(instr, 7, 0);
            return d;
        default:  // 0b00111  SUB (imm) T2
            d.op = Mnemonic::SUB_imm8;
            d.rd = d.rn = bits(instr, 10, 8); d.imm = bits(instr, 7, 0);
            return d;
    }
}

// --- A5.2.2 Data processing --------------------------------------------
DecodedInstr decode_data_processing(std::uint16_t instr) {
    DecodedInstr d;
    d.raw = instr;
    d.setflags = true;
    const std::uint8_t rdn = bits(instr, 2, 0);
    const std::uint8_t rm = bits(instr, 5, 3);
    d.rm = rm;

    static constexpr Mnemonic kOps[16] = {
        Mnemonic::AND_reg, Mnemonic::EOR_reg, Mnemonic::LSL_reg, Mnemonic::LSR_reg,
        Mnemonic::ASR_reg, Mnemonic::ADC_reg, Mnemonic::SBC_reg, Mnemonic::ROR_reg,
        Mnemonic::TST_reg, Mnemonic::RSB_imm, Mnemonic::CMP_reg, Mnemonic::CMN_reg,
        Mnemonic::ORR_reg, Mnemonic::MUL,     Mnemonic::BIC_reg, Mnemonic::MVN_reg,
    };
    d.op = kOps[bits(instr, 9, 6)];

    switch (d.op) {
        case Mnemonic::TST_reg:
        case Mnemonic::CMP_reg:
        case Mnemonic::CMN_reg:
            d.rn = rdn;  // compare only: no destination
            break;
        case Mnemonic::RSB_imm:
            d.rd = rdn; d.rn = rm; d.rm = 0; d.imm = 0;  // RSBS Rd,Rn,#0
            break;
        case Mnemonic::MUL:
            d.rd = rdn; d.rn = rm; d.rm = rdn;  // MULS Rdm,Rn,Rdm
            break;
        default:
            d.rd = d.rn = rdn;
            break;
    }
    return d;
}

// --- A5.2.3 Special data instructions and branch and exchange ----------
DecodedInstr decode_special_data(std::uint16_t instr) {
    DecodedInstr d;
    d.raw = instr;
    const std::uint8_t rm = bits(instr, 6, 3);  // 4-bit register number

    switch (bits(instr, 9, 8)) {
        case 0b00: {  // ADD (register) T2 -- high registers, does not set flags
            const std::uint8_t rdn = static_cast<std::uint8_t>((bit(instr, 7) << 3) | bits(instr, 2, 0));
            d.op = Mnemonic::ADD_reg_hi; d.rd = d.rn = rdn; d.rm = rm;
            return d;
        }
        case 0b01: {  // CMP (register) T2 -- sets flags
            const std::uint8_t rn = static_cast<std::uint8_t>((bit(instr, 7) << 3) | bits(instr, 2, 0));
            d.op = Mnemonic::CMP_reg_hi; d.rn = rn; d.rm = rm; d.setflags = true;
            return d;
        }
        case 0b10: {  // MOV (register) T1 -- high registers, does not set flags
            const std::uint8_t rd = static_cast<std::uint8_t>((bit(instr, 7) << 3) | bits(instr, 2, 0));
            d.op = Mnemonic::MOV_reg_hi; d.rd = rd; d.rm = rm;
            return d;
        }
        default: {  // 0b11  BX / BLX (register)
            if (bits(instr, 2, 0) != 0) return undef(instr);  // (0,0,0) required
            d.op = bit(instr, 7) ? Mnemonic::BLX_reg : Mnemonic::BX;
            d.rm = rm;
            return d;
        }
    }
}

// --- A5.2.4 Load/store single data item -------------------------------
DecodedInstr decode_load_store_single(std::uint16_t instr) {
    DecodedInstr d;
    d.raw = instr;
    d.add = true;
    const std::uint8_t op_a = bits(instr, 15, 12);

    if (op_a == 0b0101) {  // register offset
        static constexpr Mnemonic kOps[8] = {
            Mnemonic::STR_reg, Mnemonic::STRH_reg, Mnemonic::STRB_reg, Mnemonic::LDRSB_reg,
            Mnemonic::LDR_reg, Mnemonic::LDRH_reg, Mnemonic::LDRB_reg, Mnemonic::LDRSH_reg,
        };
        d.op = kOps[bits(instr, 11, 9)];
        d.rm = bits(instr, 8, 6);
        d.rn = bits(instr, 5, 3);
        d.rt = bits(instr, 2, 0);
        return d;
    }

    if (op_a == 0b0110 || op_a == 0b0111) {  // word/byte immediate offset
        const bool is_byte = bit(instr, 12);
        const bool is_load = bit(instr, 11);
        const std::uint8_t imm5 = bits(instr, 10, 6);
        d.rn = bits(instr, 5, 3);
        d.rt = bits(instr, 2, 0);
        if (is_byte) {
            d.op = is_load ? Mnemonic::LDRB_imm : Mnemonic::STRB_imm;
            d.imm = imm5;
        } else {
            d.op = is_load ? Mnemonic::LDR_imm : Mnemonic::STR_imm;
            d.imm = static_cast<std::uint32_t>(imm5) << 2;
        }
        return d;
    }

    if (op_a == 0b1000) {  // halfword immediate offset
        const bool is_load = bit(instr, 11);
        d.op = is_load ? Mnemonic::LDRH_imm : Mnemonic::STRH_imm;
        d.imm = static_cast<std::uint32_t>(bits(instr, 10, 6)) << 1;
        d.rn = bits(instr, 5, 3);
        d.rt = bits(instr, 2, 0);
        return d;
    }

    // op_a == 0b1001  SP-relative
    const bool is_load = bit(instr, 11);
    d.op = is_load ? Mnemonic::LDR_imm_sp : Mnemonic::STR_imm_sp;
    d.rt = bits(instr, 10, 8);
    d.rn = 13;
    d.imm = static_cast<std::uint32_t>(bits(instr, 7, 0)) << 2;
    return d;
}

// --- A5.2.5 Miscellaneous 16-bit instructions -------------------------
DecodedInstr decode_misc16(std::uint16_t instr) {
    DecodedInstr d;
    d.raw = instr;

    switch (bits(instr, 11, 8)) {
        case 0b0000: {  // ADD/SUB (SP plus/minus immediate) T2
            d.imm = static_cast<std::uint32_t>(bits(instr, 6, 0)) << 2;
            d.rd = d.rn = 13;
            if (bit(instr, 7) == 0) { d.op = Mnemonic::ADD_SP_sp_imm; d.add = true; }
            else                    { d.op = Mnemonic::SUB_SP_imm;    d.add = false; }
            return d;
        }
        case 0b0010: {  // signed/unsigned extend
            static constexpr Mnemonic kOps[4] = {
                Mnemonic::SXTH, Mnemonic::SXTB, Mnemonic::UXTH, Mnemonic::UXTB };
            d.op = kOps[bits(instr, 7, 6)];
            d.rm = bits(instr, 5, 3);
            d.rd = bits(instr, 2, 0);
            return d;
        }
        case 0b0100:
        case 0b0101: {  // PUSH
            d.op = Mnemonic::PUSH;
            d.register_list = static_cast<std::uint16_t>((static_cast<unsigned>(bit(instr, 8)) << 14) | (instr & 0xFFu));
            return d;
        }
        case 0b0110: {  // CPS (only valid misc encoding in this row for ARMv6-M)
            if (bits(instr, 7, 5) != 0b011) return undef(instr);
            d.op = Mnemonic::CPS;
            d.imm = bit(instr, 4);  // 1 => disable interrupts (CPSID), 0 => enable
            return d;
        }
        case 0b1010: {  // byte-reversal
            switch (bits(instr, 7, 6)) {
                case 0b00: d.op = Mnemonic::REV;   break;
                case 0b01: d.op = Mnemonic::REV16; break;
                case 0b11: d.op = Mnemonic::REVSH; break;
                default:   return undef(instr);
            }
            d.rm = bits(instr, 5, 3);
            d.rd = bits(instr, 2, 0);
            return d;
        }
        case 0b1100:
        case 0b1101: {  // POP
            d.op = Mnemonic::POP;
            d.register_list = static_cast<std::uint16_t>((static_cast<unsigned>(bit(instr, 8)) << 15) | (instr & 0xFFu));
            return d;
        }
        case 0b1110:  // BKPT
            d.op = Mnemonic::BKPT;
            d.imm = bits(instr, 7, 0);
            return d;
        case 0b1111: {  // hints (IT is not in ARMv6-M)
            if (bits(instr, 3, 0) != 0) return undef(instr);  // would be IT
            switch (bits(instr, 7, 4)) {
                case 0b0000: d.op = Mnemonic::NOP;   break;
                case 0b0001: d.op = Mnemonic::YIELD; break;
                case 0b0010: d.op = Mnemonic::WFE;   break;
                case 0b0011: d.op = Mnemonic::WFI;   break;
                case 0b0100: d.op = Mnemonic::SEV;   break;
                default:     d.op = Mnemonic::NOP;   break;  // other hints -> NOP
            }
            return d;
        }
        default:
            // 0b0001 / 0b0011 = CBZ, 0b1001 / 0b1011 = CBNZ, 0b0111 / 0b1000
            // reserved: none exist in ARMv6-M.
            return undef(instr);
    }
}

// --- A5.2.6 Conditional branch, and Supervisor Call -------------------
DecodedInstr decode_cond_branch(std::uint16_t instr) {
    DecodedInstr d;
    d.raw = instr;
    const std::uint8_t cond = bits(instr, 11, 8);
    if (cond == 0b1110) {  // UDF
        d.op = Mnemonic::UDF;
        d.imm = bits(instr, 7, 0);
        return d;
    }
    if (cond == 0b1111) {  // SVC
        d.op = Mnemonic::SVC;
        d.imm = bits(instr, 7, 0);
        return d;
    }
    d.op = Mnemonic::B_cond;
    d.cond = cond;
    d.branch_offset = sign_extend(static_cast<std::uint32_t>(instr & 0xFFu) << 1, 9);
    return d;
}

}  // namespace

DecodedInstr decode_thumb16(std::uint16_t instr) {
    const std::uint8_t top6 = bits(instr, 15, 10);

    if (top6 < 0b010000) return decode_shift_addsub_movcmp(instr);   // 00xxxx
    if (top6 == 0b010000) return decode_data_processing(instr);
    if (top6 == 0b010001) return decode_special_data(instr);
    if (top6 <= 0b010011) {  // 01001x  LDR (literal)
        DecodedInstr d;
        d.raw = instr;
        d.op = Mnemonic::LDR_lit;
        d.rt = bits(instr, 10, 8);
        d.rn = 15;
        d.imm = static_cast<std::uint32_t>(instr & 0xFFu) << 2;
        d.add = true;
        return d;
    }

    const std::uint8_t top4 = bits(instr, 15, 12);
    if (top4 == 0b0101 || top4 == 0b0110 || top4 == 0b0111 ||
        top4 == 0b1000 || top4 == 0b1001) {
        return decode_load_store_single(instr);
    }

    if (top4 == 0b1010) {  // ADR / ADD (SP plus immediate) T1
        DecodedInstr d;
        d.raw = instr;
        d.rd = bits(instr, 10, 8);
        d.imm = static_cast<std::uint32_t>(instr & 0xFFu) << 2;
        if (bit(instr, 11) == 0) {
            d.op = Mnemonic::ADR; d.rn = 15;
        } else {
            d.op = Mnemonic::ADD_SP_imm; d.rn = 13; d.add = true;
        }
        return d;
    }

    if (top4 == 0b1011) return decode_misc16(instr);

    if (top4 == 0b1100) {  // STM / LDM
        DecodedInstr d;
        d.raw = instr;
        const bool is_load = bit(instr, 11);
        d.op = is_load ? Mnemonic::LDM : Mnemonic::STM;
        d.rn = bits(instr, 10, 8);
        d.register_list = static_cast<std::uint16_t>(instr & 0xFFu);
        d.wback = is_load ? ((d.register_list & (1u << d.rn)) == 0) : true;
        return d;
    }

    if (top4 == 0b1101) return decode_cond_branch(instr);

    if (bits(instr, 15, 11) == 0b11100) {  // B (T2) unconditional
        DecodedInstr d;
        d.raw = instr;
        d.op = Mnemonic::B;
        d.branch_offset = sign_extend(static_cast<std::uint32_t>(instr & 0x7FFu) << 1, 12);
        return d;
    }

    return undef(instr);  // 32-bit prefixes reach decode_thumb32, not here
}

DecodedInstr decode_thumb32(std::uint16_t hw1, std::uint16_t hw2) {
    const std::uint32_t raw = (static_cast<std::uint32_t>(hw1) << 16) | hw2;

    // BL: hw1[15:11]=11110, hw2[15:14]=11, hw2[12]=1
    if (bits(hw1, 15, 11) == 0b11110 && bits(hw2, 15, 14) == 0b11 && bit(hw2, 12) == 1) {
        DecodedInstr d;
        d.raw = raw;
        d.length = 4;
        d.op = Mnemonic::BL;
        const std::uint32_t s = bit(hw1, 10);
        const std::uint32_t imm10 = hw1 & 0x3FFu;
        const std::uint32_t j1 = bit(hw2, 13);
        const std::uint32_t j2 = bit(hw2, 11);
        const std::uint32_t imm11 = hw2 & 0x7FFu;
        const std::uint32_t i1 = (j1 ^ s) ^ 1u;
        const std::uint32_t i2 = (j2 ^ s) ^ 1u;
        const std::uint32_t imm =
            (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1);
        d.branch_offset = sign_extend(imm, 25);
        return d;
    }

    // DSB / DMB / ISB: hw1 = 0xF3BF, hw2 = 0x8F4x / 0x8F5x / 0x8F6x
    if (hw1 == 0xF3BFu && (hw2 & 0xFF00u) == 0x8F00u) {
        DecodedInstr d;
        d.raw = raw; d.length = 4;
        d.imm = bits(hw2, 3, 0);  // barrier option
        switch (bits(hw2, 7, 4)) {
            case 0x4: d.op = Mnemonic::DSB; return d;
            case 0x5: d.op = Mnemonic::DMB; return d;
            case 0x6: d.op = Mnemonic::ISB; return d;
            default:  return undef(raw, 4);
        }
    }

    // MRS: hw1 = 0xF3EF, hw2 = 0x80xx (bit 15 set, Rd in [11:8], SYSm in [7:0])
    if (hw1 == 0xF3EFu && (hw2 & 0x8000u) != 0) {
        DecodedInstr d;
        d.raw = raw; d.length = 4;
        d.op = Mnemonic::MRS;
        d.rd = bits(hw2, 11, 8);
        d.imm = bits(hw2, 7, 0);  // SYSm
        return d;
    }

    // MSR: hw1 = 0xF38x (Rn in [3:0]), hw2 = 0x88xx (SYSm in [7:0])
    if ((hw1 & 0xFFF0u) == 0xF380u && (hw2 & 0xFF00u) == 0x8800u) {
        DecodedInstr d;
        d.raw = raw; d.length = 4;
        d.op = Mnemonic::MSR;
        d.rn = bits(hw1, 3, 0);
        d.imm = bits(hw2, 7, 0);  // SYSm
        return d;
    }

    return undef(raw, 4);
}

const char* to_string(Mnemonic m) {
    switch (m) {
        case Mnemonic::UNDEFINED: return "UNDEFINED";
        case Mnemonic::UNPREDICTABLE: return "UNPREDICTABLE";
        case Mnemonic::LSL_imm: return "LSL_imm";
        case Mnemonic::LSR_imm: return "LSR_imm";
        case Mnemonic::ASR_imm: return "ASR_imm";
        case Mnemonic::ADD_reg: return "ADD_reg";
        case Mnemonic::SUB_reg: return "SUB_reg";
        case Mnemonic::ADD_imm3: return "ADD_imm3";
        case Mnemonic::SUB_imm3: return "SUB_imm3";
        case Mnemonic::MOV_imm: return "MOV_imm";
        case Mnemonic::CMP_imm: return "CMP_imm";
        case Mnemonic::ADD_imm8: return "ADD_imm8";
        case Mnemonic::SUB_imm8: return "SUB_imm8";
        case Mnemonic::AND_reg: return "AND_reg";
        case Mnemonic::EOR_reg: return "EOR_reg";
        case Mnemonic::LSL_reg: return "LSL_reg";
        case Mnemonic::LSR_reg: return "LSR_reg";
        case Mnemonic::ASR_reg: return "ASR_reg";
        case Mnemonic::ADC_reg: return "ADC_reg";
        case Mnemonic::SBC_reg: return "SBC_reg";
        case Mnemonic::ROR_reg: return "ROR_reg";
        case Mnemonic::TST_reg: return "TST_reg";
        case Mnemonic::RSB_imm: return "RSB_imm";
        case Mnemonic::CMP_reg: return "CMP_reg";
        case Mnemonic::CMN_reg: return "CMN_reg";
        case Mnemonic::ORR_reg: return "ORR_reg";
        case Mnemonic::MUL: return "MUL";
        case Mnemonic::BIC_reg: return "BIC_reg";
        case Mnemonic::MVN_reg: return "MVN_reg";
        case Mnemonic::MOV_reg: return "MOV_reg";
        case Mnemonic::ADD_reg_hi: return "ADD_reg_hi";
        case Mnemonic::CMP_reg_hi: return "CMP_reg_hi";
        case Mnemonic::MOV_reg_hi: return "MOV_reg_hi";
        case Mnemonic::BX: return "BX";
        case Mnemonic::BLX_reg: return "BLX_reg";
        case Mnemonic::LDR_lit: return "LDR_lit";
        case Mnemonic::STR_reg: return "STR_reg";
        case Mnemonic::STRH_reg: return "STRH_reg";
        case Mnemonic::STRB_reg: return "STRB_reg";
        case Mnemonic::LDRSB_reg: return "LDRSB_reg";
        case Mnemonic::LDR_reg: return "LDR_reg";
        case Mnemonic::LDRH_reg: return "LDRH_reg";
        case Mnemonic::LDRB_reg: return "LDRB_reg";
        case Mnemonic::LDRSH_reg: return "LDRSH_reg";
        case Mnemonic::STR_imm: return "STR_imm";
        case Mnemonic::LDR_imm: return "LDR_imm";
        case Mnemonic::STRB_imm: return "STRB_imm";
        case Mnemonic::LDRB_imm: return "LDRB_imm";
        case Mnemonic::STRH_imm: return "STRH_imm";
        case Mnemonic::LDRH_imm: return "LDRH_imm";
        case Mnemonic::STR_imm_sp: return "STR_imm_sp";
        case Mnemonic::LDR_imm_sp: return "LDR_imm_sp";
        case Mnemonic::ADR: return "ADR";
        case Mnemonic::ADD_SP_imm: return "ADD_SP_imm";
        case Mnemonic::ADD_SP_sp_imm: return "ADD_SP_sp_imm";
        case Mnemonic::SUB_SP_imm: return "SUB_SP_imm";
        case Mnemonic::SXTH: return "SXTH";
        case Mnemonic::SXTB: return "SXTB";
        case Mnemonic::UXTH: return "UXTH";
        case Mnemonic::UXTB: return "UXTB";
        case Mnemonic::PUSH: return "PUSH";
        case Mnemonic::POP: return "POP";
        case Mnemonic::REV: return "REV";
        case Mnemonic::REV16: return "REV16";
        case Mnemonic::REVSH: return "REVSH";
        case Mnemonic::CPS: return "CPS";
        case Mnemonic::BKPT: return "BKPT";
        case Mnemonic::NOP: return "NOP";
        case Mnemonic::YIELD: return "YIELD";
        case Mnemonic::WFE: return "WFE";
        case Mnemonic::WFI: return "WFI";
        case Mnemonic::SEV: return "SEV";
        case Mnemonic::STM: return "STM";
        case Mnemonic::LDM: return "LDM";
        case Mnemonic::B_cond: return "B_cond";
        case Mnemonic::SVC: return "SVC";
        case Mnemonic::UDF: return "UDF";
        case Mnemonic::B: return "B";
        case Mnemonic::BL: return "BL";
        case Mnemonic::MSR: return "MSR";
        case Mnemonic::MRS: return "MRS";
        case Mnemonic::DSB: return "DSB";
        case Mnemonic::DMB: return "DMB";
        case Mnemonic::ISB: return "ISB";
    }
    return "?";
}

}  // namespace rp2040
