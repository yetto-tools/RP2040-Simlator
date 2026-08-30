// Unit tests for the System Control Space peripheral (SysTick / NVIC / SCB).
// Reference: ARMv6-M ARM B3; RP2040 datasheet 2.4.
#include "doctest.h"

#include <cstdint>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "core/scs.h"
#include "exceptions.h"

using namespace rp2040;

namespace {

struct ScsFix {
    RegisterFile regs;
    Memory mem;
    Cpu cpu{regs, mem};
    Scs scs{cpu};

    ScsFix() { REQUIRE(scs.attach(mem)); }

    std::uint32_t rd(std::uint32_t off) { return mem.read_word(Scs::kBase + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(Scs::kBase + off, v) == BusStatus::Ok);
    }
};

}  // namespace

TEST_CASE_FIXTURE(ScsFix, "PPB region is now routed to the SCS") {
    // Before this peripheral existed, 0xE000E000 faulted as InvalidAddress.
    CHECK(mem.read_word(0xE000ED00u).status == BusStatus::Ok);
    CHECK(rd(0xD00) == Scs::kCpuid);  // CPUID = Cortex-M0+
}

TEST_CASE_FIXTURE(ScsFix, "AIRCR.SYSRESETREQ invokes the system-reset hook (with the right key)") {
    int resets = 0;
    scs.on_system_reset([&] { ++resets; });

    wr(0xD0C, 0x00000004u);          // SYSRESETREQ set, but VECTKEY missing
    CHECK(resets == 0);

    wr(0xD0C, 0x05FA0004u);          // VECTKEY | SYSRESETREQ
    CHECK(resets == 1);

    wr(0xD0C, 0x05FA0000u);          // key ok, bit clear
    CHECK(resets == 1);
}

TEST_CASE_FIXTURE(ScsFix, "VTOR read/write mirrors the CPU") {
    wr(0xD08, 0x20001000u);
    CHECK(cpu.vtor() == 0x20001000u);
    CHECK(rd(0xD08) == 0x20001000u);
    wr(0xD08, 0x10004037u);           // low 7 bits ignored (128-byte aligned)
    CHECK(cpu.vtor() == 0x10004000u);
}

TEST_CASE_FIXTURE(ScsFix, "NVIC enable / disable via ISER / ICER") {
    wr(0x100, (1u << 3) | (1u << 7));   // NVIC_ISER: enable IRQ3, IRQ7
    CHECK(cpu.irq_enabled(3));
    CHECK(cpu.irq_enabled(7));
    CHECK_FALSE(cpu.irq_enabled(4));
    CHECK(rd(0x100) == ((1u << 3) | (1u << 7)));

    wr(0x180, (1u << 3));               // NVIC_ICER: disable IRQ3
    CHECK_FALSE(cpu.irq_enabled(3));
    CHECK(cpu.irq_enabled(7));
}

TEST_CASE_FIXTURE(ScsFix, "NVIC pending via ISPR / ICPR") {
    wr(0x200, (1u << 5));               // NVIC_ISPR: pend IRQ5
    CHECK(cpu.is_pending(kExcExternal0 + 5));
    CHECK(rd(0x200) == (1u << 5));
    wr(0x280, (1u << 5));               // NVIC_ICPR
    CHECK_FALSE(cpu.is_pending(kExcExternal0 + 5));
}

TEST_CASE_FIXTURE(ScsFix, "NVIC_IPR byte lanes map to per-IRQ priority") {
    wr(0x400, 0xC0'80'40'00u);          // IPR0: IRQ0=0x00 IRQ1=0x40 IRQ2=0x80 IRQ3=0xC0
    CHECK(cpu.exception_priority(kExcExternal0 + 1) == 0x40);
    CHECK(cpu.exception_priority(kExcExternal0 + 3) == 0xC0);
    CHECK(rd(0x400) == 0xC0'80'40'00u);
}

TEST_CASE_FIXTURE(ScsFix, "SHPR2/SHPR3 set the system-handler priorities") {
    wr(0xD1C, 0xC0u << 24);             // SHPR2: SVCall priority 3
    CHECK(cpu.exception_priority(kExcSVCall) == 0xC0);

    wr(0xD20, (0x40u << 24) | (0x80u << 16));  // SHPR3: SysTick=1, PendSV=2
    CHECK(cpu.exception_priority(kExcSysTick) == 0x40);
    CHECK(cpu.exception_priority(kExcPendSV) == 0x80);
    CHECK(rd(0xD20) == ((0x40u << 24) | (0x80u << 16)));
}

TEST_CASE_FIXTURE(ScsFix, "ICSR pends / clears PendSV and SysTick") {
    wr(0xD04, 1u << 28);               // PENDSVSET
    CHECK(cpu.is_pending(kExcPendSV));
    CHECK((rd(0xD04) & (1u << 28)) != 0);
    wr(0xD04, 1u << 27);               // PENDSVCLR
    CHECK_FALSE(cpu.is_pending(kExcPendSV));

    wr(0xD04, 1u << 26);               // PENDSTSET
    CHECK(cpu.is_pending(kExcSysTick));
}

TEST_CASE_FIXTURE(ScsFix, "ICSR VECTACTIVE reflects the running exception") {
    regs.set_exception_number(kExcHardFault);
    CHECK((rd(0xD04) & 0x1FFu) == kExcHardFault);
}

TEST_CASE_FIXTURE(ScsFix, "AIRCR ignores writes without the vector key") {
    wr(0xD0C, 0x00000004u);            // SYSRESETREQ, no key -> ignored
    CHECK(rd(0xD0C) == (0x05FAu << 16));
}

TEST_CASE_FIXTURE(ScsFix, "SysTick counts down, wraps, sets COUNTFLAG") {
    wr(0x014, 4);                       // SYST_RVR = 4
    wr(0x018, 0);                       // SYST_CVR = 0 (forces reload on first tick)
    wr(0x010, 0x1);                     // SYST_CSR: ENABLE, no TICKINT

    scs.on_cycles(1);                   // 0 -> reload to 4
    CHECK(scs.systick_cvr() == 4);
    scs.on_cycles(3);                   // 4 -> 1
    CHECK(scs.systick_cvr() == 1);
    CHECK_FALSE(scs.systick_countflag());
    scs.on_cycles(1);                   // 1 -> 0 : COUNTFLAG
    CHECK(scs.systick_cvr() == 0);
    CHECK(scs.systick_countflag());

    CHECK((rd(0x010) & (1u << 16)) != 0);   // read COUNTFLAG ...
    CHECK_FALSE(scs.systick_countflag());   // ... and it clears
}

TEST_CASE_FIXTURE(ScsFix, "SysTick with TICKINT pends the SysTick exception") {
    wr(0x014, 2);
    wr(0x018, 0);
    wr(0x010, 0x3);                     // ENABLE | TICKINT
    scs.on_cycles(1);                   // reload -> 2
    scs.on_cycles(2);                   // 2 -> 1 -> 0
    CHECK(cpu.is_pending(kExcSysTick));
}

TEST_CASE_FIXTURE(ScsFix, "an enabled + pended IRQ is delivered through step()") {
    constexpr std::uint32_t base = 0x20000000u;
    constexpr std::uint32_t vtab = 0x20001000u;
    cpu.set_vtor(vtab);
    REQUIRE(mem.write_word(vtab + 0, 0x20002000u) == BusStatus::Ok);     // MSP
    REQUIRE(mem.write_word(vtab + 4, (base | 1u)) == BusStatus::Ok);     // reset PC
    REQUIRE(mem.write_word(vtab + 4u * (kExcExternal0 + 2), 0x20000201u) == BusStatus::Ok);
    const std::uint16_t prog[] = {0x2000, 0x2001, 0xE7FE};  // movs r0,#0 ; movs r0,#1 ; b .
    REQUIRE(mem.load(base, prog, sizeof(prog)));
    cpu.reset();

    REQUIRE(cpu.step() == ExecStatus::Ok);        // movs r0,#0
    wr(0x100, 1u << 2);                           // enable IRQ2
    wr(0x200, 1u << 2);                           // pend IRQ2
    CHECK(cpu.step() == ExecStatus::ExceptionTaken);
    CHECK(regs.pc() == 0x20000200u);
    CHECK(regs.exception_number() == kExcExternal0 + 2);
    CHECK(regs.get(0) == 0);                      // 'movs r0,#1' was preempted

    // Not enabled -> stays pending, does not fire.
    cpu.clear_pending(kExcExternal0 + 2);
    wr(0x180, 1u << 2);                           // disable IRQ2
    wr(0x200, 1u << 2);                           // pend again
    // back in thread mode first
    regs.set_exception_number(0);
    CHECK(cpu.step() == ExecStatus::Ok);
    CHECK(cpu.is_pending(kExcExternal0 + 2));
}
