// Integration test: load a real arm-none-eabi-gcc Cortex-M0+ ELF and run it
// end to end (ELF loader -> Memory -> decoder -> executor -> ALU/flags).
//
// The fixture ELF path is injected by CMake as RP2040_FIRMWARE_SUM_ELF when
// the ARM toolchain is available; otherwise the cases below report as skipped.
#include "doctest.h"

#include <cstdint>
#include <vector>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "loaders/elf_loader.h"
#include "loaders/uf2_loader.h"

using namespace rp2040;

#ifdef RP2040_FIRMWARE_SUM_ELF

namespace {

// Wrap a flat [base, base+bytes.size()) image into a well-formed RP2040 UF2,
// exactly as pico-sdk's elf2uf2 would.
std::vector<std::uint8_t> wrap_uf2(std::uint32_t base, const std::vector<std::uint8_t>& bytes) {
    constexpr std::uint32_t kChunk = 256;
    const std::uint32_t total = (static_cast<std::uint32_t>(bytes.size()) + kChunk - 1) / kChunk;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(total) * 512u, 0u);
    auto wr = [&](std::size_t off, std::uint32_t v) {
        out[off + 0] = static_cast<std::uint8_t>(v);
        out[off + 1] = static_cast<std::uint8_t>(v >> 8);
        out[off + 2] = static_cast<std::uint8_t>(v >> 16);
        out[off + 3] = static_cast<std::uint8_t>(v >> 24);
    };
    for (std::uint32_t i = 0; i < total; ++i) {
        const std::size_t b = static_cast<std::size_t>(i) * 512u;
        const std::uint32_t start = i * kChunk;
        const std::uint32_t n = (start + kChunk <= bytes.size())
                                    ? kChunk
                                    : static_cast<std::uint32_t>(bytes.size()) - start;
        wr(b + 0, 0x0A324655u);
        wr(b + 4, 0x9E5D5157u);
        wr(b + 8, 0x00002000u);  // familyID present
        wr(b + 12, base + start);
        wr(b + 16, n);
        wr(b + 20, i);
        wr(b + 24, total);
        wr(b + 28, kUf2FamilyRp2040);
        for (std::uint32_t k = 0; k < n; ++k) out[b + 32 + k] = bytes[start + k];
        wr(b + 508, 0x0AB16F30u);
    }
    return out;
}

}  // namespace

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

TEST_CASE("sum.elf repackaged as UF2 loads and runs identically") {
    // Load the real ELF once to discover its byte image, then round-trip that
    // image through the UF2 container and run it on a fresh machine.
    Memory ref;
    const ElfImage img = load_elf_file(ref, RP2040_FIRMWARE_SUM_ELF);
    REQUIRE_MESSAGE(img.ok, img.error);

    const std::uint32_t base = img.lowest_addr;
    std::vector<std::uint8_t> flat(img.highest_addr - base, 0u);
    REQUIRE(ref.dump(base, flat.data(), flat.size()));

    const std::vector<std::uint8_t> uf2 = wrap_uf2(base, flat);

    Memory mem;
    RegisterFile regs;
    Cpu cpu(regs, mem);
    const Uf2Image u = load_uf2(mem, uf2.data(), uf2.size());
    REQUIRE_MESSAGE(u.ok, u.error);
    CHECK(u.family_id == kUf2FamilyRp2040);
    CHECK(u.lowest_addr == base);

    regs.set_msp(kSramBase + kSramSize);
    regs.set_pc(img.entry & ~std::uint32_t{1});
    regs.set_thumb(true);

    int guard = 200000;
    while (guard-- > 0) {
        const std::uint32_t pc = regs.pc();
        REQUIRE(cpu.step() == ExecStatus::Ok);
        if (regs.pc() == pc) break;
    }
    REQUIRE(guard > 0);

    bool found = false;
    for (std::uint32_t a = kSramBase; a < img.highest_addr + 64u && !found; a += 4u) {
        if (mem.read_word(a).value == 1055u) found = true;
    }
    CHECK_MESSAGE(found, "expected g_result == 1055 after the UF2 round-trip");
}

#else

TEST_CASE("firmware integration" * doctest::skip()) {
    MESSAGE("arm-none-eabi-gcc not available at configure time");
}

#endif
