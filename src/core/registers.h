// registers.h - ARM Cortex-M0+ (ARMv6-M) register file (BACKLOG P1.1).
//
// Holds the 16 core registers, the banked stack pointers, the program status
// register fields (APSR/IPSR/EPSR) and the CONTROL/PRIMASK special registers,
// plus ARMv6-M condition-code evaluation.
//
// Reference: ARMv6-M Architecture Reference Manual (DDI 0419), sections B1.4
// ("Registers") and A6.3 ("Conditional execution"). See ARCHITECTURE.md
// section 1.2 / 1.5.
//
// Scope note: the register file stores the *raw* PC. The architectural rule
// that a program read of R15 yields "address of current instruction + 4"
// belongs to the execute stage (P1.2), not here.
#ifndef RP2040_CORE_REGISTERS_H
#define RP2040_CORE_REGISTERS_H

#include <array>
#include <cstdint>

namespace rp2040 {

enum class CpuMode { Thread, Handler };

// ARMv6-M condition codes (ARMv6-M ARM, Table A6-1). The 4-bit encoding
// 0b1111 has no mnemonic - it is reserved and never denotes a condition on
// this architecture (the Bcc T1 space reuses it for SVC).
enum class Condition : std::uint8_t {
    EQ = 0x0, NE = 0x1, CS = 0x2, CC = 0x3,
    MI = 0x4, PL = 0x5, VS = 0x6, VC = 0x7,
    HI = 0x8, LS = 0x9, GE = 0xA, LT = 0xB,
    GT = 0xC, LE = 0xD, AL = 0xE,
};

class RegisterFile {
public:
    static constexpr unsigned kSP = 13;
    static constexpr unsigned kLR = 14;
    static constexpr unsigned kPC = 15;

    // Cortex-M reset value of the link register (ARMv6-M ARM B1.4.1).
    static constexpr std::uint32_t kLrResetValue = 0xFFFFFFFFu;

    RegisterFile() { reset(); }

    // Architectural reset: R0-R12 = 0, LR = 0xFFFFFFFF, PC = 0, both SPs = 0,
    // Thread mode, Thumb state, all flags clear, CONTROL = 0, PRIMASK = 0.
    void reset();

    // --- General register access (n = 0..15) ------------------------------
    // n == 13 targets the currently-active stack pointer (see sp()).
    // n == 13 write forces bits [1:0] to 0; n == 15 write forces bit 0 to 0.
    std::uint32_t get(unsigned n) const;
    void set(unsigned n, std::uint32_t value);

    // --- Program counter -------------------------------------------------
    std::uint32_t pc() const { return pc_; }
    void set_pc(std::uint32_t value) { pc_ = value & ~std::uint32_t{1}; }
    void advance_pc(std::uint32_t bytes) { pc_ += bytes; }

    // --- Link register --------------------------------------------------
    std::uint32_t lr() const { return lr_; }
    void set_lr(std::uint32_t value) { lr_ = value; }

    // --- Stack pointers (banked: MSP / PSP) ------------------------------
    // Handler mode always uses MSP. Thread mode uses PSP iff CONTROL.SPSEL.
    std::uint32_t sp() const;
    void set_sp(std::uint32_t value);           // active SP, [1:0] forced 0
    std::uint32_t msp() const { return msp_; }
    std::uint32_t psp() const { return psp_; }
    void set_msp(std::uint32_t v) { msp_ = v & ~std::uint32_t{3}; }
    void set_psp(std::uint32_t v) { psp_ = v & ~std::uint32_t{3}; }

    // --- APSR flags ----------------------------------------------------
    bool n() const { return n_; }
    bool z() const { return z_; }
    bool c() const { return c_; }
    bool v() const { return v_; }
    void set_n(bool b) { n_ = b; }
    void set_z(bool b) { z_ = b; }
    void set_c(bool b) { c_ = b; }
    void set_v(bool b) { v_ = b; }
    void set_flags(bool n_flag, bool z_flag, bool c_flag, bool v_flag);
    // Convenience for logical / move ops: N from bit 31, Z from result == 0.
    void set_nz_from(std::uint32_t result);

    // --- Program status register views -------------------------------
    std::uint32_t apsr() const;   // N,Z,C,V in [31:28]
    std::uint32_t ipsr() const;   // exception number in [8:0]
    std::uint32_t epsr() const;   // T bit in [24]
    std::uint32_t xpsr() const;   // apsr | ipsr | epsr
    void set_apsr(std::uint32_t value);   // consumes [31:28]
    void set_xpsr(std::uint32_t value);   // exception-return restore

    bool thumb() const { return t_; }
    void set_thumb(bool t) { t_ = t; }

    // --- Mode / current exception -----------------------------------
    CpuMode mode() const { return exception_ == 0 ? CpuMode::Thread : CpuMode::Handler; }
    std::uint16_t exception_number() const { return exception_; }
    void set_exception_number(std::uint16_t e) { exception_ = e & 0x1FFu; }

    // --- CONTROL ---------------------------------------------------
    bool control_npriv() const { return npriv_; }   // bit 0: unprivileged thread
    bool control_spsel() const { return spsel_; }   // bit 1: use PSP in thread mode
    void set_control(bool npriv, bool spsel) { npriv_ = npriv; spsel_ = spsel; }

    // --- PRIMASK --------------------------------------------------
    bool primask() const { return primask_; }
    void set_primask(bool m) { primask_ = m; }

    // --- Condition evaluation (ARMv6-M ARM, ConditionPassed pseudocode) ---
    bool condition_holds(std::uint8_t cond4) const;
    bool condition_holds(Condition cond) const {
        return condition_holds(static_cast<std::uint8_t>(cond));
    }

private:
    std::array<std::uint32_t, 13> r_{};   // R0..R12
    std::uint32_t pc_ = 0;
    std::uint32_t lr_ = kLrResetValue;
    std::uint32_t msp_ = 0;
    std::uint32_t psp_ = 0;

    bool n_ = false;
    bool z_ = false;
    bool c_ = false;
    bool v_ = false;
    bool t_ = true;                 // Thumb state - always 1 on Cortex-M
    std::uint16_t exception_ = 0;   // IPSR; 0 => Thread mode
    bool npriv_ = false;
    bool spsel_ = false;
    bool primask_ = false;
};

}  // namespace rp2040

#endif  // RP2040_CORE_REGISTERS_H
