// Unit tests for the Cortex-M0+ register file (BACKLOG P1.1).
// Reference: ARMv6-M ARM sections B1.4 and A6.3; ARCHITECTURE.md 1.2 / 1.5.
#include "doctest.h"

#include <cstdint>

#include "core/registers.h"

using rp2040::Condition;
using rp2040::CpuMode;
using rp2040::RegisterFile;

TEST_CASE("reset establishes the architectural initial state") {
    RegisterFile rf;
    for (unsigned i = 0; i < 13; ++i) {
        CAPTURE(i);
        CHECK(rf.get(i) == 0u);
    }
    CHECK(rf.lr() == 0xFFFFFFFFu);
    CHECK(rf.pc() == 0u);
    CHECK(rf.sp() == 0u);
    CHECK(rf.mode() == CpuMode::Thread);
    CHECK(rf.thumb());
    CHECK_FALSE(rf.n());
    CHECK_FALSE(rf.z());
    CHECK_FALSE(rf.c());
    CHECK_FALSE(rf.v());
    CHECK_FALSE(rf.primask());
    CHECK(rf.xpsr() == (std::uint32_t{1} << 24));  // only EPSR.T set
}

TEST_CASE("general registers R0..R12 round-trip") {
    RegisterFile rf;
    for (unsigned i = 0; i < 13; ++i) {
        rf.set(i, 0x1000u + i);
    }
    for (unsigned i = 0; i < 13; ++i) {
        CAPTURE(i);
        CHECK(rf.get(i) == 0x1000u + i);
    }
}

TEST_CASE("index 13/14/15 alias SP/LR/PC") {
    RegisterFile rf;
    rf.set(RegisterFile::kLR, 0xAABBCCDDu);
    CHECK(rf.get(14) == 0xAABBCCDDu);
    CHECK(rf.lr() == 0xAABBCCDDu);

    rf.set(RegisterFile::kPC, 0x10000200u);
    CHECK(rf.get(15) == 0x10000200u);
    CHECK(rf.pc() == 0x10000200u);
}

TEST_CASE("SP writes force word alignment, via any path") {
    RegisterFile rf;
    rf.set_sp(0x20001003u);
    CHECK(rf.sp() == 0x20001000u);

    rf.set(RegisterFile::kSP, 0x20002007u);
    CHECK(rf.get(13) == 0x20002004u);
}

TEST_CASE("PC writes clear bit 0 (Thumb address, no interworking here)") {
    RegisterFile rf;
    rf.set_pc(0x10000101u);
    CHECK(rf.pc() == 0x10000100u);
    rf.advance_pc(2);
    CHECK(rf.pc() == 0x10000102u);
}

TEST_CASE("stack pointer banking follows mode and CONTROL.SPSEL") {
    RegisterFile rf;
    rf.set_msp(0x20040000u);
    rf.set_psp(0x20030000u);

    // Thread mode, SPSEL = 0 -> MSP
    CHECK(rf.sp() == 0x20040000u);

    // Thread mode, SPSEL = 1 -> PSP
    rf.set_control(/*npriv=*/false, /*spsel=*/true);
    CHECK(rf.sp() == 0x20030000u);
    rf.set_sp(0x2003F000u);
    CHECK(rf.psp() == 0x2003F000u);
    CHECK(rf.msp() == 0x20040000u);  // untouched

    // Handler mode always uses MSP regardless of SPSEL
    rf.set_exception_number(15);
    CHECK(rf.mode() == CpuMode::Handler);
    CHECK(rf.sp() == 0x20040000u);
    rf.set_sp(0x2003E000u);
    CHECK(rf.msp() == 0x2003E000u);
    CHECK(rf.psp() == 0x2003F000u);  // untouched
}

TEST_CASE("set_nz_from derives N and Z") {
    RegisterFile rf;
    rf.set_nz_from(0u);
    CHECK(rf.z());
    CHECK_FALSE(rf.n());

    rf.set_nz_from(0x80000000u);
    CHECK_FALSE(rf.z());
    CHECK(rf.n());

    rf.set_nz_from(0x7FFFFFFFu);
    CHECK_FALSE(rf.z());
    CHECK_FALSE(rf.n());
}

TEST_CASE("APSR occupies bits [31:28] in the documented order") {
    RegisterFile rf;
    rf.set_flags(true, false, true, false);
    CHECK(rf.apsr() == 0xA0000000u);  // N=1 Z=0 C=1 V=0

    rf.set_flags(false, true, false, true);
    CHECK(rf.apsr() == 0x50000000u);  // N=0 Z=1 C=0 V=1

    rf.set_apsr(0xF0000000u);
    CHECK((rf.n() && rf.z() && rf.c() && rf.v()));
}

TEST_CASE("IPSR reports the exception number and drives mode") {
    RegisterFile rf;
    CHECK(rf.ipsr() == 0u);
    CHECK(rf.mode() == CpuMode::Thread);

    rf.set_exception_number(3);  // HardFault
    CHECK(rf.ipsr() == 3u);
    CHECK(rf.mode() == CpuMode::Handler);

    rf.set_exception_number(0);
    CHECK(rf.mode() == CpuMode::Thread);
}

TEST_CASE("xPSR combines APSR, IPSR and EPSR; set_xpsr restores all three") {
    RegisterFile rf;
    rf.set_flags(true, true, false, false);  // N,Z
    rf.set_exception_number(11);              // SVCall
    const std::uint32_t expected =
        0xC0000000u | (std::uint32_t{1} << 24) | 11u;
    CHECK(rf.xpsr() == expected);

    RegisterFile restored;
    restored.set_xpsr(expected);
    CHECK(restored.n());
    CHECK(restored.z());
    CHECK_FALSE(restored.c());
    CHECK(restored.thumb());
    CHECK(restored.exception_number() == 11u);
}

TEST_CASE("CONTROL and PRIMASK are independent one-bit registers") {
    RegisterFile rf;
    rf.set_control(true, false);
    CHECK(rf.control_npriv());
    CHECK_FALSE(rf.control_spsel());

    rf.set_primask(true);
    CHECK(rf.primask());
    CHECK(rf.control_npriv());  // unchanged
}

namespace {

// Evaluate a 4-bit condition against an explicit N,Z,C,V flag set.
bool eval(std::uint8_t cond, bool n, bool z, bool c, bool v) {
    RegisterFile rf;
    rf.set_flags(n, z, c, v);
    return rf.condition_holds(cond);
}

}  // namespace

TEST_CASE("condition codes: single-flag conditions") {
    // EQ/NE test Z
    CHECK(eval(0x0, false, true, false, false));
    CHECK_FALSE(eval(0x1, false, true, false, false));
    CHECK(eval(0x1, false, false, false, false));

    // CS/CC test C
    CHECK(eval(0x2, false, false, true, false));
    CHECK_FALSE(eval(0x3, false, false, true, false));

    // MI/PL test N
    CHECK(eval(0x4, true, false, false, false));
    CHECK(eval(0x5, false, false, false, false));

    // VS/VC test V
    CHECK(eval(0x6, false, false, false, true));
    CHECK_FALSE(eval(0x7, false, false, false, true));
}

TEST_CASE("condition codes: compound conditions") {
    // HI = C && !Z ; LS = !C || Z
    CHECK(eval(0x8, false, false, true, false));
    CHECK_FALSE(eval(0x8, false, true, true, false));
    CHECK(eval(0x9, false, true, true, false));
    CHECK(eval(0x9, false, false, false, false));

    // GE = (N == V) ; LT = (N != V)
    CHECK(eval(0xA, true, false, false, true));    // N==V
    CHECK_FALSE(eval(0xA, true, false, false, false));
    CHECK(eval(0xB, true, false, false, false));   // N!=V

    // GT = !Z && (N == V) ; LE = Z || (N != V)
    CHECK(eval(0xC, false, false, false, false));
    CHECK_FALSE(eval(0xC, false, true, false, false));
    CHECK(eval(0xD, false, true, false, false));
    CHECK(eval(0xD, true, false, false, false));   // N!=V
}

TEST_CASE("condition codes: AL and the reserved 0b1111 encoding are always true") {
    CHECK(eval(0xE, false, false, false, false));
    CHECK(eval(0xE, true, true, true, true));
    // 0b1111 must NOT be inverted despite the odd low bit.
    CHECK(eval(0xF, false, false, false, false));
    CHECK(eval(0xF, true, true, true, true));
    CHECK(rp2040::RegisterFile{}.condition_holds(Condition::AL));
}
