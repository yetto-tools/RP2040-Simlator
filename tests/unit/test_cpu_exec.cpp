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

TEST_CASE_FIXTURE(CpuFix, "WFI sleeps until an interrupt pends, then resumes past it") {
    // wfi ; movs r0,#1 ; b .
    program({0xBF30, 0x2001, 0xE7FE});
    regs.set_sp(kBase + 0x800);
    cpu.set_irq_enabled(0, true);

    CHECK(step() == ExecStatus::WaitingForInterrupt);   // executes WFI, now asleep
    CHECK(cpu.asleep());
    CHECK(step() == ExecStatus::WaitingForInterrupt);   // still idle
    CHECK(regs.get(0) == 0u);

    cpu.pend_exception(kExcExternal0);                  // IRQ0 arrives
    const ExecStatus s = step();
    CHECK(s == ExecStatus::ExceptionTaken);
    CHECK_FALSE(cpu.asleep());
}

TEST_CASE_FIXTURE(CpuFix, "WFE consumes a pending event instead of sleeping; SEV wakes it") {
    program({0xBF20, 0xBF20, 0x2001, 0xE7FE});  // wfe ; wfe ; movs r0,#1 ; b .

    cpu.signal_event();
    CHECK(step() == ExecStatus::Ok);            // first WFE eats the event
    CHECK_FALSE(cpu.asleep());

    CHECK(step() == ExecStatus::WaitingForInterrupt);  // second WFE sleeps
    CHECK(cpu.asleep());
    cpu.signal_event();                         // an SEV from elsewhere
    CHECK_FALSE(cpu.asleep());
    CHECK(step() == ExecStatus::Ok);            // resumes: movs r0,#1
    CHECK(regs.get(0) == 1u);
}

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

TEST_CASE_FIXTURE(CpuFix, "a v7-M-only encoding takes a HardFault") {
    // Bare execute() still exposes the raw UNDEFINED signal.
    CHECK(cpu.execute(decode_thumb16(0xBF08 /* ITE EQ */), kBase) == ExecStatus::Undefined);

    // Through step(), UNDEFINED vectors to HardFault. Put the vector table in
    // SRAM (ROM is read-only) and point VTOR at it.
    regs.set_msp(0x20002000u);
    cpu.set_vtor(0x20001000u);
    REQUIRE(mem.write_word(0x20001000u + 4u * 3u, 0x20000201u) == BusStatus::Ok);
    program({0xBF08});
    CHECK(cpu.step() == ExecStatus::ExceptionTaken);
    CHECK(regs.pc() == 0x20000200u);
    CHECK(regs.exception_number() == 3u);   // HardFault
}

// --- memory-access slice -------------------------------------------------

namespace {
constexpr std::uint32_t kData = 0x20001000u;  // scratch data area in SRAM
}

TEST_CASE_FIXTURE(CpuFix, "word LDR/STR: immediate, register offset, SP-relative") {
    regs.set(1, kData);
    regs.set(0, 0xDEADBEEFu);
    CHECK(run_one(cpu, regs, 0x6008) == ExecStatus::Ok);  // str r0, [r1, #0]
    CHECK(mem.read_word(kData).value == 0xDEADBEEFu);
    CHECK(run_one(cpu, regs, 0x680A) == ExecStatus::Ok);  // ldr r2, [r1, #0]
    CHECK(regs.get(2) == 0xDEADBEEFu);

    regs.set(2, 4);
    regs.set(0, 0x11112222u);
    run_one(cpu, regs, 0x5088);                            // str r0, [r1, r2]
    run_one(cpu, regs, 0x588B);                            // ldr r3, [r1, r2]
    CHECK(regs.get(3) == 0x11112222u);
    CHECK(mem.read_word(kData + 4).value == 0x11112222u);

    regs.set_msp(0x20002000u);
    regs.set(0, 0xCAFEF00Du);
    run_one(cpu, regs, 0x9001);                            // str r0, [sp, #4]
    run_one(cpu, regs, 0x9901);                            // ldr r1, [sp, #4]
    CHECK(regs.get(1) == 0xCAFEF00Du);
}

TEST_CASE_FIXTURE(CpuFix, "LDR literal reads from Align(PC,4) + imm") {
    program({0x4801 /* ldr r0, [pc, #4] */, 0xBF00 /* nop */,
             0x0000, 0x0000 /* literal at 0x08 */});
    const std::array<std::uint8_t, 4> lit{0x0D, 0xF0, 0xAD, 0x0B};
    REQUIRE(mem.load(kBase + 8, lit.data(), lit.size()));
    CHECK(cpu.step() == ExecStatus::Ok);
    CHECK(regs.get(0) == 0x0BADF00Du);
}

TEST_CASE_FIXTURE(CpuFix, "byte and halfword loads zero- or sign-extend") {
    regs.set(1, kData);
    const std::array<std::uint8_t, 4> bytes{0x80, 0x81, 0x00, 0x00};
    REQUIRE(mem.load(kData, bytes.data(), bytes.size()));

    run_one(cpu, regs, 0x780A);  // ldrb r2, [r1, #0]
    CHECK(regs.get(2) == 0x80);
    regs.set(0, 0);
    run_one(cpu, regs, 0x560A);  // ldrsb r2, [r1, r0]
    CHECK(regs.get(2) == 0xFFFFFF80u);

    run_one(cpu, regs, 0x880A);  // ldrh r2, [r1, #0]
    CHECK(regs.get(2) == 0x8180u);
    run_one(cpu, regs, 0x5E0A);  // ldrsh r2, [r1, r0]
    CHECK(regs.get(2) == 0xFFFF8180u);
}

TEST_CASE_FIXTURE(CpuFix, "byte/halfword stores only touch their width") {
    regs.set(1, kData);
    REQUIRE(mem.write_word(kData, 0xFFFFFFFFu) == BusStatus::Ok);
    regs.set(0, 0x1234AB5Au);

    run_one(cpu, regs, 0x7008);  // strb r0, [r1, #0]
    CHECK(mem.read_word(kData).value == 0xFFFFFF5Au);
    run_one(cpu, regs, 0x8008);  // strh r0, [r1, #0]
    CHECK(mem.read_word(kData).value == 0xFFFFAB5Au);
}

TEST_CASE_FIXTURE(CpuFix, "unaligned word/halfword access faults") {
    regs.set(1, kData + 1);
    CHECK(run_one(cpu, regs, 0x680A) == ExecStatus::MemFault);  // ldr, addr%4 != 0
    CHECK(run_one(cpu, regs, 0x880A) == ExecStatus::MemFault);  // ldrh, addr odd
    CHECK(run_one(cpu, regs, 0x780A) == ExecStatus::Ok);        // ldrb: any alignment
}

TEST_CASE_FIXTURE(CpuFix, "store into the XIP flash window faults") {
    regs.set(1, 0x10000000u);
    regs.set(0, 1);
    CHECK(run_one(cpu, regs, 0x6008) == ExecStatus::MemFault);  // str r0, [r1, #0]
}

TEST_CASE_FIXTURE(CpuFix, "PUSH / POP round-trip and adjust SP") {
    regs.set_msp(0x20002000u);
    regs.set(4, 0xAAAA0001u);
    regs.set(5, 0xBBBB0002u);
    regs.set(14, 0x20000123u);

    run_one(cpu, regs, 0xB530);  // push {r4, r5, lr}
    CHECK(regs.sp() == 0x20002000u - 12);
    CHECK(mem.read_word(0x20001FF4u).value == 0xAAAA0001u);  // lowest reg, lowest addr
    CHECK(mem.read_word(0x20001FFCu).value == 0x20000123u);  // lr on top

    regs.set(4, 0);
    regs.set(5, 0);
    run_one(cpu, regs, 0xBC30);  // pop {r4, r5}
    CHECK(regs.get(4) == 0xAAAA0001u);
    CHECK(regs.get(5) == 0xBBBB0002u);
    CHECK(regs.sp() == 0x20002000u - 4);
}

TEST_CASE_FIXTURE(CpuFix, "POP {..., PC} branches with the Thumb bit") {
    regs.set_msp(0x20001FFCu);
    REQUIRE(mem.write_word(0x20001FFCu, 0x20000401u) == BusStatus::Ok);
    regs.set_pc(kBase + 2);
    CHECK(cpu.execute(decode_thumb16(0xBD00 /* pop {pc} */), kBase) == ExecStatus::Ok);
    CHECK(regs.pc() == 0x20000400u);
    CHECK(regs.thumb());
    CHECK(regs.sp() == 0x20002000u);
}

TEST_CASE_FIXTURE(CpuFix, "STMIA / LDMIA transfer in register order and write back") {
    regs.set(0, kData);
    regs.set(1, 0x1000);
    regs.set(2, 0x2000);
    regs.set(3, 0x3000);
    run_one(cpu, regs, 0xC00E);  // stmia r0!, {r1, r2, r3}
    CHECK(regs.get(0) == kData + 12);
    CHECK(mem.read_word(kData + 0).value == 0x1000);
    CHECK(mem.read_word(kData + 8).value == 0x3000);

    regs.set(4, kData);
    run_one(cpu, regs, 0xCC07);  // ldmia r4!, {r0, r1, r2}
    CHECK(regs.get(0) == 0x1000);
    CHECK(regs.get(2) == 0x3000);
    CHECK(regs.get(4) == kData + 12);
}

TEST_CASE_FIXTURE(CpuFix, "LDMIA with base in the list does not write back") {
    regs.set(0, kData);
    const std::array<std::uint8_t, 8> d8{1, 0, 0, 0, 2, 0, 0, 0};
    REQUIRE(mem.load(kData, d8.data(), d8.size()));
    run_one(cpu, regs, 0xC803);  // ldmia r0, {r0, r1}
    CHECK(regs.get(0) == 1);     // overwritten by the load, not the writeback
    CHECK(regs.get(1) == 2);
}

TEST_CASE_FIXTURE(CpuFix, "step() runs a subroutine call/return") {
    // main:  bl func ; b .
    // func:  push {lr} ; movs r0,#7 ; pop {pc}
    program({
        0xF000, 0xF801,  // 0x00: bl 0x06 (offset: (0x00+4)+2 = 0x06)
        0xE7FE,          // 0x04: b .
        0xB500,          // 0x06: push {lr}
        0x2007,          // 0x08: movs r0, #7
        0xBD00,          // 0x0A: pop {pc}
    });
    regs.set_msp(0x20002000u);

    int guard = 50;
    while (guard-- > 0) {
        REQUIRE(cpu.step() == ExecStatus::Ok);
        if (regs.pc() == kBase + 0x04) break;
    }
    CHECK(regs.get(0) == 7);
    CHECK(regs.pc() == kBase + 0x04);
    CHECK(regs.sp() == 0x20002000u);   // balanced
}
