#include "core/alu.h"

namespace rp2040 {

AddResult add_with_carry(std::uint32_t x, std::uint32_t y, bool carry_in) {
    const std::uint64_t cin = carry_in ? 1u : 0u;
    const std::uint64_t usum = static_cast<std::uint64_t>(x) + y + cin;
    const std::uint32_t result = static_cast<std::uint32_t>(usum);

    AddResult r;
    r.value = result;
    r.carry = (usum >> 32) != 0;
    // Signed overflow: x and y share a sign that the result does not.
    r.overflow = ((~(x ^ y) & (x ^ result)) & 0x80000000u) != 0;
    return r;
}

ShiftResult shift_c(std::uint32_t value, SRType type, unsigned amount, bool carry_in) {
    if (amount == 0) {
        return {value, carry_in};
    }

    switch (type) {
        case SRType::LSL: {
            if (amount > 32) return {0u, false};
            const std::uint64_t wide = static_cast<std::uint64_t>(value) << amount;
            return {static_cast<std::uint32_t>(wide), ((wide >> 32) & 1u) != 0};
        }
        case SRType::LSR: {
            if (amount > 32) return {0u, false};
            const std::uint32_t res = (amount == 32) ? 0u : (value >> amount);
            const bool carry = ((value >> (amount - 1)) & 1u) != 0;
            return {res, carry};
        }
        case SRType::ASR: {
            if (amount >= 32) {
                const bool neg = (value & 0x80000000u) != 0;
                return {neg ? 0xFFFFFFFFu : 0u, neg};
            }
            const std::uint32_t res =
                static_cast<std::uint32_t>(static_cast<std::int32_t>(value) >> amount);
            const bool carry = ((value >> (amount - 1)) & 1u) != 0;
            return {res, carry};
        }
        case SRType::ROR: {
            const unsigned m = amount & 31u;
            const std::uint32_t res =
                (m == 0) ? value : ((value >> m) | (value << (32u - m)));
            return {res, (res & 0x80000000u) != 0};
        }
        case SRType::RRX: {
            const std::uint32_t res = (value >> 1) | (static_cast<std::uint32_t>(carry_in) << 31);
            return {res, (value & 1u) != 0};
        }
    }
    return {value, carry_in};  // unreachable
}

}  // namespace rp2040
