// Unit tests for Cortex-M0+ instruction timing (BACKLOG P1.5).
// Reference: Cortex-M0+ TRM (DDI 0484) Table 3-1.
#include "doctest.h"

#include <cstdint>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "core/timing.h"
#include "thumb_isa.h"

using namespace rp2040;

namespace {
unsigned cyc(std::uint16_t word, bool took = false, unsigned n = 0) {
    return instruction_cycles(decode_thumb16(word), took, n);
}
}  // namespace

TEST_CASE("single-cycle data processing") {
    CHECK(cyc(0x1888) == 1);   // adds r0, r1, r2
    CHECK(cyc(0x4008) == 1);   // ands r0, r1
    CHECK(cyc(0x0088) == 1);   // lsls r0, r1, #2
    CHECK(cyc(0x2000) == 1);   // movs r0, #0
    CHECK(cyc(0x4348) == 1);   // muls  - single-cycle multiplier on RP2040
    CHECK(cyc(0xBF00) == 1);   // nop
    CHECK(cyc(0xB662) == 1);   // cpsie i
}

TEST_CASE("loads and stores are two cycles") {
    CHECK(cyc(0x6808) == 2);   // ldr  r0, [r1]
    CHECK(cyc(0x6008) == 2);   // str  r0, [r1]
    CHECK(cyc(0x7808) == 2);   // ldrb
    CHECK(cyc(0x8808) == 2);   // ldrh
    CHECK(cyc(0x4800) == 2);   // ldr r0, [pc, #0]
    CHECK(cyc(0x9800) == 2);   // ldr r0, [sp, #0]
}

TEST_CASE("multi-register transfers cost 1 + N") {
    CHECK(cyc(0xC00E, false, 3) == 4);   // stmia r0!, {r1,r2,r3}
    CHECK(cyc(0xCC07, false, 3) == 4);   // ldmia r4!, {r0,r1,r2}
    CHECK(cyc(0xB407, false, 3) == 4);   // push {r0,r1,r2}
    CHECK(cyc(0xBC07, false, 3) == 4);   // pop  {r0,r1,r2}
}

TEST_CASE("POP with PC pays a pipeline reload (4 + N)") {
    CHECK(cyc(0xBD07, false, 4) == 8);   // pop {r0,r1,r2,pc}  -> 4 + 4
    CHECK(cyc(0xBD00, false, 1) == 5);   // pop {pc}           -> 4 + 1
}

TEST_CASE("branch timings") {
    CHECK(cyc(0xE7FE) == 3);             // b .            (unconditional)
    CHECK(cyc(0xD0FE, /*took=*/true) == 3);
    CHECK(cyc(0xD0FE, /*took=*/false) == 1);
    CHECK(cyc(0x4770) == 3);             // bx lr
    CHECK(instruction_cycles(decode_thumb32(0xF000, 0xF800), false, 0) == 4);  // bl
}

TEST_CASE("hi-register move/add to PC reloads the pipeline") {
    auto pc_move = decode_thumb16(0x4687);          // mov pc, r0
    REQUIRE(pc_move.op == Mnemonic::MOV_reg_hi);
    REQUIRE(pc_move.rd == 15);
    CHECK(instruction_cycles(pc_move, false, 0) == 3);

    CHECK(cyc(0x4640) == 1);                         // mov r0, r8  -> rd != 15
}

TEST_CASE("system instructions") {
    CHECK(instruction_cycles(decode_thumb32(0xF3EF, 0x8000), false, 0) == 4);  // mrs
    CHECK(instruction_cycles(decode_thumb32(0xF3BF, 0x8F5F), false, 0) == 3);  // dmb
    CHECK(cyc(0xBF30) == 2);             // wfi
}

TEST_CASE("step() accumulates cycles for a real loop") {
    RegisterFile regs;
    Memory mem;
    Cpu cpu(regs, mem);
    constexpr std::uint32_t base = 0x20000000u;
    regs.set_pc(base);

    const std::uint16_t prog[] = {
        0x2003,  // movs r0, #3     -> 1
        0x1c40,  // adds r0, r0, #1 -> 1
        0x3801,  // subs r0, #1     -> 1
        0xd1fc,  // bne .-8         -> 3 taken / 1 not taken
        0xe7fe,  // b .             -> 3
    };
    REQUIRE(mem.load(base, prog, sizeof(prog)));

    REQUIRE(cpu.step() == ExecStatus::Ok);   // movs
    CHECK(cpu.cycle_count() == 1);
    REQUIRE(cpu.step() == ExecStatus::Ok);   // adds
    CHECK(cpu.cycle_count() == 2);
    REQUIRE(cpu.step() == ExecStatus::Ok);   // subs -> r0 = 3, Z clear
    CHECK(cpu.cycle_count() == 3);
    REQUIRE(cpu.step() == ExecStatus::Ok);   // bne taken
    CHECK(cpu.cycle_count() == 6);           // +3

    cpu.reset_cycle_count();
    CHECK(cpu.cycle_count() == 0);
}
