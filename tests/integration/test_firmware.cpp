// Integration test: load a real arm-none-eabi-gcc Cortex-M0+ ELF and run it
// end to end (ELF loader -> Memory -> decoder -> executor -> ALU/flags).
//
// The fixture ELF path is injected by CMake as RP2040_FIRMWARE_SUM_ELF when
// the ARM toolchain is available; otherwise the cases below report as skipped.
#include "doctest.h"

#include <cstdint>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "loaders/elf_loader.h"

using namespace rp2040;

#ifdef RP2040_FIRMWARE_SUM_ELF

TEST_CASE("sum.elf: optimised GCC output runs to its result") {
    Memory mem;
    RegisterFile regs;
    Cpu cpu(regs, mem);

    const ElfImage img = load_elf_file(mem, RP2040_FIRMWARE_SUM_ELF);
    REQUIRE_MESSAGE(img.ok, img.error);
    CHECK(img.segments_loaded >= 1);

    // Bare image (no vector table): start at the entry point, SP at top of SRAM.
    regs.set_msp(kSramBase + kSramSize);
    regs.set_pc(img.entry & ~std::uint32_t{1});
    regs.set_thumb(true);

    std::uint32_t prev_pc = ~0u;
    int guard = 200000;
    while (guard-- > 0) {
        const std::uint32_t pc = regs.pc();
        REQUIRE(cpu.step() == ExecStatus::Ok);
        if (regs.pc() == pc) { prev_pc = pc; break; }  // reached the spin loop
    }
    REQUIRE(guard > 0);
    CHECK(prev_pc != ~0u);

    // Find g_result: it is the .bss word the program stored 1055 into
    // (sum(1..10) == 55, plus 1000 because BSS read back as zero).
    bool found = false;
    for (std::uint32_t a = kSramBase; a < img.highest_addr + 64u && !found; a += 4u) {
        if (mem.read_word(a).value == 1055u) found = true;
    }
    CHECK_MESSAGE(found, "expected g_result == 1055 somewhere in .bss");

    CHECK(cpu.cycle_count() > 0);
}

#else

TEST_CASE("firmware integration" * doctest::skip()) {
    MESSAGE("arm-none-eabi-gcc not available at configure time");
}

#endif
