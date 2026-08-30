// alu.h - ARMv6-M arithmetic/shift primitives with flag outputs.
//
// These mirror the ARM ARM pseudocode functions AddWithCarry() and Shift_C()
// exactly (ARMv6-M ARM, sections A2.2.1 and A2.2.4). Keeping them isolated and
// separately tested is what makes the instruction executor auditable.
#ifndef RP2040_CORE_ALU_H
#define RP2040_CORE_ALU_H

#include <cstdint>

namespace rp2040 {

struct AddResult {
    std::uint32_t value;
    bool carry;
    bool overflow;
};

// AddWithCarry(x, y, carry_in): the single primitive behind ADD/ADC/SUB/SBC/
// RSB/CMP/CMN/NEG. For subtraction pass (x, ~y, /*carry_in=*/true).
AddResult add_with_carry(std::uint32_t x, std::uint32_t y, bool carry_in);

enum class SRType { LSL, LSR, ASR, ROR, RRX };

struct ShiftResult {
    std::uint32_t value;
    bool carry;
};

// Shift_C(value, type, amount, carry_in). `amount` is the already-decoded
// shift count (for LSR/ASR immediate the caller passes 32 when imm5 == 0, per
// DecodeImmShift). An amount of 0 returns the value untouched with carry_in.
ShiftResult shift_c(std::uint32_t value, SRType type, unsigned amount, bool carry_in);

}  // namespace rp2040

#endif  // RP2040_CORE_ALU_H
