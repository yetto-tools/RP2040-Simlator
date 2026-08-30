#include "core/registers.h"

#include <cassert>

namespace rp2040 {

void RegisterFile::reset() {
    r_.fill(0);
    pc_ = 0;
    lr_ = kLrResetValue;
    msp_ = 0;
    psp_ = 0;
    n_ = z_ = c_ = v_ = false;
    t_ = true;
    exception_ = 0;
    npriv_ = false;
    spsel_ = false;
    primask_ = false;
}

// --- general register access -------------------------------------------

std::uint32_t RegisterFile::get(unsigned n) const {
    assert(n < 16);
    if (n < 13) return r_[n];
    if (n == kSP) return sp();
    if (n == kLR) return lr_;
    return pc_;  // kPC
}

void RegisterFile::set(unsigned n, std::uint32_t value) {
    assert(n < 16);
    if (n < 13) {
        r_[n] = value;
    } else if (n == kSP) {
        set_sp(value);
    } else if (n == kLR) {
        lr_ = value;
    } else {  // kPC
        set_pc(value);
    }
}

// --- banked stack pointer ---------------------------------------------

std::uint32_t RegisterFile::sp() const {
    if (mode() == CpuMode::Handler) return msp_;
    return spsel_ ? psp_ : msp_;
}

void RegisterFile::set_sp(std::uint32_t value) {
    const std::uint32_t aligned = value & ~std::uint32_t{3};
    if (mode() == CpuMode::Handler || !spsel_) {
        msp_ = aligned;
    } else {
        psp_ = aligned;
    }
}

// --- flags -----------------------------------------------------------

void RegisterFile::set_flags(bool n_flag, bool z_flag, bool c_flag, bool v_flag) {
    n_ = n_flag;
    z_ = z_flag;
    c_ = c_flag;
    v_ = v_flag;
}

void RegisterFile::set_nz_from(std::uint32_t result) {
    n_ = (result & 0x80000000u) != 0;
    z_ = (result == 0);
}

// --- program status register views ---------------------------------

std::uint32_t RegisterFile::apsr() const {
    return (std::uint32_t{n_} << 31) | (std::uint32_t{z_} << 30) |
           (std::uint32_t{c_} << 29) | (std::uint32_t{v_} << 28);
}

std::uint32_t RegisterFile::ipsr() const {
    return std::uint32_t{exception_} & 0x1FFu;
}

std::uint32_t RegisterFile::epsr() const {
    return std::uint32_t{t_} << 24;
}

std::uint32_t RegisterFile::xpsr() const {
    return apsr() | ipsr() | epsr();
}

void RegisterFile::set_apsr(std::uint32_t value) {
    n_ = (value & (std::uint32_t{1} << 31)) != 0;
    z_ = (value & (std::uint32_t{1} << 30)) != 0;
    c_ = (value & (std::uint32_t{1} << 29)) != 0;
    v_ = (value & (std::uint32_t{1} << 28)) != 0;
}

void RegisterFile::set_xpsr(std::uint32_t value) {
    set_apsr(value);
    t_ = (value & (std::uint32_t{1} << 24)) != 0;
    exception_ = static_cast<std::uint16_t>(value & 0x1FFu);
}

// --- condition evaluation -------------------------------------------
// Mirrors the ARMv6-M ARM ConditionPassed() pseudocode.

bool RegisterFile::condition_holds(std::uint8_t cond4) const {
    bool result = false;
    switch (cond4 >> 1) {
        case 0x0: result = z_; break;                    // EQ / NE
        case 0x1: result = c_; break;                    // CS / CC
        case 0x2: result = n_; break;                    // MI / PL
        case 0x3: result = v_; break;                    // VS / VC
        case 0x4: result = c_ && !z_; break;             // HI / LS
        case 0x5: result = (n_ == v_); break;            // GE / LT
        case 0x6: result = (n_ == v_) && !z_; break;     // GT / LE
        default:  result = true; break;                  // AL (0b111x)
    }
    // Invert for the odd encoding, except never for 0b1111.
    if ((cond4 & 1) != 0 && cond4 != 0x0F) {
        result = !result;
    }
    return result;
}

}  // namespace rp2040
