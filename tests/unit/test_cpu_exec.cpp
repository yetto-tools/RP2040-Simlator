// Unit tests for the ARMv6-M instruction executor (Cpu).
// Covers the computational core + branches; memory ops land in the next slice.
#include "doctest.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "thumb_isa.h"

using namespace rp2040;

namespace {

constexpr std::uint32_t kBase = 0x20000000u;  // SRAM

// Fixture: a CPU wired to a fresh register file + memory, PC at kBase.
struct CpuFix {
    RegisterFile regs;
    Memory mem;
    Cpu cpu{regs, mem};

    CpuFix() { regs.set_pc(kBase); }

    // Load a sequence of 16-bit instruction words at kBase (host is LE).
    void program(std::initializer_list<std::uint16_t> words) {
        std::vector<std::uint16_t> v(words);
        REQUIRE(mem.load(kBase, v.data(), v.size() * sizeof(std::uint16_t)));
    }

    ExecStatus step() { return cpu.step(); }
};

// Decode a single 16-bit word and execute it as if it sat at `pc`.
ExecStatus run_one(Cpu& cpu, RegisterFile& regs, std::uint16_t word, std::uint32_t pc = kBase) {
    regs.set_pc(pc + 2);
    return cpu.execute(decode_thumb16(word), pc);
}

}  // namespace

TEST_CASE_FIXTURE(CpuFix, "MOVS #imm sets N/Z, leaves C/V") {
    regs.set_c(true);
    regs.set_v(true);
    CHECK(run_one(cpu, regs, 0x2000) == ExecStatus::Ok);  // movs r0, #0
    CHECK(regs.get(0) == 0);
    CHECK(regs.z());
    CHECK_FALSE(regs.n());
    CHECK(regs.c());   // unchanged
    CHECK(regs.v());   // unchanged
}

TEST_CASE_FIXTURE(CpuFix, "ADDS computes carry and overflow") {
    regs.set(0, 0xFFFFFFFFu);
    regs.set(1, 1);
    CHECK(run_one(cpu, regs, 0x1808) == ExecStatus::Ok);  // adds r0, r1, r0
    CHECK(regs.get(0) == 0);
    CHECK(regs.z());
    CHECK(regs.c());
    CHECK_FALSE(regs.v());
}

TEST_CASE_FIXTURE(CpuFix, "SUBS sets carry (no borrow) and clears it (borrow)") {
    regs.set(1, 5);
    regs.set(2, 3);
    run_one(cpu, regs, 0x1A88 /* subs r0, r1, r2 */);
    CHECK(regs.get(0) == 2);
    CHECK(regs.c());           // 5 - 3, no borrow
    CHECK_FALSE(regs.n());

    regs.set(1, 3);
    regs.set(2, 5);
    run_one(cpu, regs, 0x1A88);
    CHECK(regs.get(0) == 0xFFFFFFFEu);
    CHECK_FALSE(regs.c());     // borrow
    CHECK(regs.n());
}

TEST_CASE_FIXTURE(CpuFix, "CMP is a flag-only subtract") {
    regs.set(0, 42);
    run_one(cpu, regs, 0x280A /* cmp r0, #10 */);
    CHECK(regs.get(0) == 42);  // unchanged
    CHECK_FALSE(regs.z());
    CHECK(regs.c());           // 42 >= 10 unsigned

    regs.set(0, 10);
    run_one(cpu, regs, 0x280A);
    CHECK(regs.z());
    CHECK(regs.c());
}

TEST_CASE_FIXTURE(CpuFix, "shifts produce the shifter carry") {
    regs.set(1, 0x80000000u);
    run_one(cpu, regs, 0x0048 /* lsls r0, r1, #1 */);
    CHECK(regs.get(0) == 0);
    CHECK(regs.z());
    CHECK(regs.c());           // bit shifted out

    regs.set(1, 0x00000003u);
    run_one(cpu, regs, 0x0849 /* lsrs r1, r1, #1 */);
    CHECK(regs.get(1) == 1);
    CHECK(regs.c());           // bit0 was set

    regs.set(1, 0x80000000u);
    run_one(cpu, regs, 0x1049 /* asrs r1, r1, #1 */);
    CHECK(regs.get(1) == 0xC0000000u);
}

TEST_CASE_FIXTURE(CpuFix, "logical ops set N/Z only") {
    regs.set_c(true);
    regs.set(0, 0xF0F0F0F0u);
    regs.set(1, 0x0F0F0F0Fu);
    run_one(cpu, regs, 0x4008 /* ands r0, r1 */);
    CHECK(regs.get(0) == 0);
    CHECK(regs.z());
    CHECK(regs.c());           // preserved

    regs.set(0, 0x00000005u);
    regs.set(1, 0x00000003u);
    run_one(cpu, regs, 0x4048 /* eors r0, r1 */);
    CHECK(regs.get(0) == 0x6);
}

TEST_CASE_FIXTURE(CpuFix, "MULS keeps low 32 bits, sets N/Z") {
    regs.set(0, 0x10000);
    regs.set(1, 0x10000);
    run_one(cpu, regs, 0x4348 /* muls r0, r1, r0 */);
    CHECK(regs.get(0) == 0);   // 2^32 truncated
    CHECK(regs.z());
}

TEST_CASE_FIXTURE(CpuFix, "high-register MOV/ADD reach r8-r14 and PC") {
    regs.set(9, 0xABCD);
    run_one(cpu, regs, 0x46C8 /* mov r8, r9 */);
    CHECK(regs.get(8) == 0xABCD);

    regs.set(8, 0x100);
    regs.set(9, 0x020);
    run_one(cpu, regs, 0x44C8 /* add r8, r9 */);
    CHECK(regs.get(8) == 0x120);

    // add pc, r0  -> PC = (instr_pc + 4) + r0, bit0 cleared
    regs.set(0, 0x10);
    run_one(cpu, regs, 0x4478 /* add r0, pc ... actually add r0,pc */);
    CHECK(regs.get(0) == kBase + 4 + 0x10);
}

TEST_CASE_FIXTURE(CpuFix, "ADR / ADD(SP) use aligned PC and SP") {
    regs.set_msp(0x20040000u);
    run_one(cpu, regs, 0xA004 /* adr r0, #16 */);
    CHECK(regs.get(0) == ((kBase + 4) & ~3u) + 16);

    run_one(cpu, regs, 0xA904 /* add r1, sp, #16 */);
    CHECK(regs.get(1) == 0x20040010u);

    run_one(cpu, regs, 0xB002 /* add sp, #8 */);
    CHECK(regs.sp() == 0x20040008u);
    run_one(cpu, regs, 0xB082 /* sub sp, #8 */);
    CHECK(regs.sp() == 0x20040000u);
}

TEST_CASE_FIXTURE(CpuFix, "sign/zero extends") {
    regs.set(1, 0x000080FFu);
    run_one(cpu, regs, 0xB248 /* sxtb r0, r1 */);
    CHECK(regs.get(0) == 0xFFFFFFFFu);
    run_one(cpu, regs, 0xB2C8 /* uxtb r0, r1 */);
    CHECK(regs.get(0) == 0xFF);
    regs.set(1, 0x0000FEEDu);
    run_one(cpu, regs, 0xB208 /* sxth r0, r1 */);
    CHECK(regs.get(0) == 0xFFFFFEEDu);
}

TEST_CASE_FIXTURE(CpuFix, "REV / REV16 / REVSH") {
    regs.set(1, 0x11223344u);
    run_one(cpu, regs, 0xBA08 /* rev r0, r1 */);
    CHECK(regs.get(0) == 0x44332211u);
    run_one(cpu, regs, 0xBA48 /* rev16 r0, r1 */);
    CHECK(regs.get(0) == 0x22114433u);
    regs.set(1, 0x0000FF80u);
    run_one(cpu, regs, 0xBAC8 /* revsh r0, r1 */);
    CHECK(regs.get(0) == 0xFFFF80FFu);
}

TEST_CASE_FIXTURE(CpuFix, "branches: B, BL, BX, conditional") {
    SUBCASE("unconditional B adds a signed offset to PC+4") {
        regs.set_pc(kBase + 2);
        CHECK(cpu.execute(decode_thumb16(0xE7FE), kBase) == ExecStatus::Ok);
        CHECK(regs.pc() == kBase);            // b .  -> offset -4, (kBase+4)-4
    }
    SUBCASE("BL links the return address with the Thumb bit") {
        auto d = decode_thumb32(0xF000, 0xF800);  // bl +0
        regs.set_pc(kBase + 4);
        CHECK(cpu.execute(d, kBase) == ExecStatus::Ok);
        CHECK(regs.lr() == ((kBase + 4) | 1u));
        CHECK(regs.pc() == kBase + 4);
    }
    SUBCASE("BX to LR returns, updating the T bit") {
        regs.set_lr(0x20001001u);
        regs.set_pc(kBase + 2);
        CHECK(cpu.execute(decode_thumb16(0x4770), kBase) == ExecStatus::Ok);
        CHECK(regs.pc() == 0x20001000u);
        CHECK(regs.thumb());
    }
    SUBCASE("Bcc taken vs not taken") {
        regs.set_z(true);
        regs.set_pc(kBase + 2);
        cpu.execute(decode_thumb16(0xD0FE), kBase);  // beq .-4  (offset -4)
        CHECK(regs.pc() == kBase);
        regs.set_z(false);
        regs.set_pc(kBase + 2);
        cpu.execute(decode_thumb16(0xD0FE), kBase);
        CHECK(regs.pc() == kBase + 2);               // fell through
    }
}

TEST_CASE_FIXTURE(CpuFix, "MRS / MSR round-trip PRIMASK and APSR") {
    regs.set_flags(true, false, true, false);       // N, C
    auto mrs = decode_thumb32(0xF3EF, 0x8000);      // mrs r0, APSR (SYSm 0)
    regs.set_pc(kBase + 4);
    cpu.execute(mrs, kBase);
    CHECK(regs.get(0) == 0xA0000000u);

    regs.set(1, 0x1u);
    auto msr = decode_thumb32(0xF381, 0x8810);      // msr PRIMASK, r1
    regs.set_pc(kBase + 4);
    cpu.execute(msr, kBase);
    CHECK(regs.primask());
}

TEST_CASE_FIXTURE(CpuFix, "BKPT and SVC surface to the caller") {
    CHECK(run_one(cpu, regs, 0xBEAB) == ExecStatus::Breakpoint);
    CHECK(cpu.last_bkpt_imm() == 0xAB);
    CHECK(run_one(cpu, regs, 0xDF2A) == ExecStatus::Svc);
    CHECK(cpu.last_svc_imm() == 0x2A);
}

TEST_CASE_FIXTURE(CpuFix, "step() runs a real accumulate loop") {
    // r0 = 5; r1 = 0; do { r1 += r0; } while (--r0); then spin.
    program({
        0x2005,  // 0x00: movs r0, #5
        0x2100,  // 0x02: movs r1, #0
        0x1809,  // 0x04: adds r1, r1, r0
        0x3801,  // 0x06: subs r0, #1
        0xD1FC,  // 0x08: bne  0x04
        0xE7FE,  // 0x0A: b .
    });

    int guard = 200;
    while (guard-- > 0) {
        REQUIRE(cpu.step() == ExecStatus::Ok);
        if (regs.pc() == kBase + 0x0A) break;
    }
    CHECK(regs.get(0) == 0);
    CHECK(regs.get(1) == 15);   // 5+4+3+2+1
    CHECK(regs.pc() == kBase + 0x0A);
}

TEST_CASE_FIXTURE(CpuFix, "step() reports UNDEFINED for a v7-M-only encoding") {
    program({0xBF08});  // ITE EQ -> not in ARMv6-M
    CHECK(cpu.step() == ExecStatus::Undefined);
}
