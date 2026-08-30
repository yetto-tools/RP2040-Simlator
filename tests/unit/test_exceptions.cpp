// Unit tests for the ARMv6-M exception model (BACKLOG P1.4).
// Reference: ARMv6-M ARM section B1.5.
#include "doctest.h"

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "exceptions.h"
#include "thumb_isa.h"

using namespace rp2040;

namespace {

constexpr std::uint32_t kCode  = 0x20000000u;
constexpr std::uint32_t kVtor  = 0x20001000u;  // vector table in SRAM
constexpr std::uint32_t kStack = 0x20002000u;  // initial MSP

struct ExcFix {
    RegisterFile regs;
    Memory mem;
    Cpu cpu{regs, mem};

    ExcFix() {
        cpu.set_vtor(kVtor);
        write_vector(0, kStack);          // initial SP
        write_vector(1, kCode | 1u);      // reset PC (Thumb bit)
    }
    void write_vector(unsigned n, std::uint32_t value) {
        REQUIRE(mem.write_word(kVtor + 4u * n, value) == BusStatus::Ok);
    }
    void code(std::uint32_t at, std::initializer_list<std::uint16_t> words) {
        std::vector<std::uint16_t> v(words);
        REQUIRE(mem.load(at, v.data(), v.size() * sizeof(std::uint16_t)));
    }
};

}  // namespace

TEST_CASE_FIXTURE(ExcFix, "reset loads MSP and PC from the vector table") {
    cpu.reset();
    CHECK(regs.sp() == kStack);
    CHECK(regs.pc() == kCode);
    CHECK(regs.thumb());
    CHECK(regs.mode() == CpuMode::Thread);
}

TEST_CASE_FIXTURE(ExcFix, "exception entry stacks the 8-word frame and vectors") {
    cpu.reset();
    regs.set(0, 0x1111);
    regs.set(1, 0x2222);
    regs.set(2, 0x3333);
    regs.set(3, 0x4444);
    regs.set(12, 0xCCCC);
    regs.set(14, 0xEEEE);
    regs.set_flags(true, true, false, false);  // N,Z -> xPSR[31:30]
    write_vector(kExcSysTick, 0x20000401u);

    const std::uint32_t ret = 0x20000040u;
    CHECK(cpu.take_exception(kExcSysTick, ret) == ExecStatus::ExceptionTaken);

    const std::uint32_t fp = kStack - kStackFrameBytes;  // was 8-aligned already
    CHECK(regs.sp() == fp);
    CHECK(mem.read_word(fp + 4u * kFrameR0).value == 0x1111);
    CHECK(mem.read_word(fp + 4u * kFrameR3).value == 0x4444);
    CHECK(mem.read_word(fp + 4u * kFrameR12).value == 0xCCCC);
    CHECK(mem.read_word(fp + 4u * kFrameLR).value == 0xEEEE);
    CHECK(mem.read_word(fp + 4u * kFrameReturnAddress).value == ret);
    CHECK((mem.read_word(fp + 4u * kFrameXPSR).value & 0xC0000000u) == 0xC0000000u);

    CHECK(regs.exception_number() == kExcSysTick);
    CHECK(regs.mode() == CpuMode::Handler);
    CHECK(regs.lr() == kExcReturnThreadMSP);
    CHECK(regs.pc() == 0x20000400u);
}

TEST_CASE_FIXTURE(ExcFix, "misaligned SP on entry is realigned and flagged") {
    cpu.reset();
    regs.set_msp(kStack - 4);          // 8-byte-misaligned
    write_vector(kExcSysTick, 0x20000401u);
    cpu.take_exception(kExcSysTick, 0x20000040u);

    const std::uint32_t fp = (kStack - 4 - kStackFrameBytes) & ~std::uint32_t{7};
    CHECK(regs.sp() == fp);
    CHECK((mem.read_word(fp + 4u * kFrameXPSR).value & kXpsrStackAlign) != 0);
}

TEST_CASE_FIXTURE(ExcFix, "SVC vectors to SVCall, handler runs, BX LR returns") {
    // main:   svc #7 ; movs r1, #0xAA ; b .
    code(kCode, {0xDF07, 0x21AA, 0xE7FE});
    // SVCall handler: movs r4, #0x42 ; bx lr
    code(0x20000100u, {0x2442, 0x4770});
    write_vector(kExcSVCall, 0x20000101u);
    cpu.reset();

    CHECK(cpu.step() == ExecStatus::ExceptionTaken);   // svc -> entry
    CHECK(regs.exception_number() == kExcSVCall);
    CHECK(regs.pc() == 0x20000100u);

    CHECK(cpu.step() == ExecStatus::Ok);               // movs r4, #0x42
    CHECK(cpu.step() == ExecStatus::Ok);               // bx lr -> exception return
    CHECK(regs.mode() == CpuMode::Thread);
    CHECK(regs.exception_number() == 0u);
    CHECK(regs.sp() == kStack);                        // stack balanced
    CHECK(regs.pc() == kCode + 2);                     // resumes after svc
    CHECK(regs.get(4) == 0x42);                        // r4 change survives
    CHECK(regs.get(0) == 0);                           // r0..r3 restored from frame

    CHECK(cpu.step() == ExecStatus::Ok);               // movs r1, #0xAA
    CHECK(regs.get(1) == 0xAA);
    CHECK(cpu.last_svc_imm() == 7);
}

TEST_CASE_FIXTURE(ExcFix, "handler that pushed LR returns via POP {PC}") {
    code(kCode, {0xDF00, 0xE7FE});                     // svc #0 ; b .
    // handler: push {lr} ; movs r4,#1 ; pop {pc}
    code(0x20000200u, {0xB500, 0x2401, 0xBD00});
    write_vector(kExcSVCall, 0x20000201u);
    cpu.reset();

    REQUIRE(cpu.step() == ExecStatus::ExceptionTaken);
    REQUIRE(cpu.step() == ExecStatus::Ok);             // push {lr}
    REQUIRE(cpu.step() == ExecStatus::Ok);             // movs r4,#1
    CHECK(cpu.step() == ExecStatus::Ok);               // pop {pc} -> exception return
    CHECK(regs.mode() == CpuMode::Thread);
    CHECK(regs.sp() == kStack);
    CHECK(regs.pc() == kCode + 2);
    CHECK(regs.get(4) == 1);
}

TEST_CASE_FIXTURE(ExcFix, "a pended exception is taken between instructions") {
    code(kCode, {0x2001 /* movs r0,#1 */, 0x2002 /* movs r0,#2 */, 0xE7FE});
    write_vector(kExcSysTick, 0x20000301u);
    code(0x20000300u, {0xE7FE});                       // handler: spin
    cpu.reset();

    REQUIRE(cpu.step() == ExecStatus::Ok);             // movs r0,#1
    cpu.pend_exception(kExcSysTick);
    CHECK(cpu.step() == ExecStatus::ExceptionTaken);   // SysTick, not movs r0,#2
    CHECK(regs.exception_number() == kExcSysTick);
    CHECK(regs.get(0) == 1);                           // second movs didn't run
    // return address on the frame is the instruction that was next
    CHECK(mem.read_word(regs.sp() + 4u * kFrameReturnAddress).value == kCode + 2);
    CHECK_FALSE(cpu.is_pending(kExcSysTick));          // moved from pending to active
}

TEST_CASE_FIXTURE(ExcFix, "PRIMASK blocks a configurable-priority exception") {
    code(kCode, {0x2000, 0xE7FE});
    write_vector(kExcSysTick, 0x20000301u);
    cpu.reset();
    regs.set_primask(true);

    cpu.pend_exception(kExcSysTick);
    CHECK(cpu.step() == ExecStatus::Ok);               // masked -> runs movs
    CHECK(regs.exception_number() == 0u);
    CHECK(cpu.is_pending(kExcSysTick));                // still pending

    regs.set_primask(false);
    CHECK(cpu.step() == ExecStatus::ExceptionTaken);   // now delivered
}

TEST_CASE_FIXTURE(ExcFix, "higher-priority pending preempts a running handler") {
    cpu.reset();
    cpu.set_exception_priority(kExcSysTick, 0x80);     // priority 2
    cpu.set_exception_priority(kExcPendSV, 0x00);      // priority 0 (higher)
    write_vector(kExcSysTick, 0x20000301u);
    write_vector(kExcPendSV, 0x20000401u);
    code(0x20000300u, {0xBF00, 0xE7FE});               // SysTick handler: nop; spin

    // Enter SysTick.
    REQUIRE(cpu.take_exception(kExcSysTick, kCode) == ExecStatus::ExceptionTaken);
    CHECK(cpu.current_execution_priority() == 2);

    // PendSV (priority 0) pends -> preempts SysTick on the next step.
    cpu.pend_exception(kExcPendSV);
    CHECK(cpu.step() == ExecStatus::ExceptionTaken);
    CHECK(regs.exception_number() == kExcPendSV);
    CHECK(regs.lr() == kExcReturnHandlerMSP);          // nested: returning to a handler
}
