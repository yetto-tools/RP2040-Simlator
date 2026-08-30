// Unit tests for the ARM ELF32 loader (BACKLOG P7.1, pulled forward).
#include "doctest.h"

#include <cstdint>
#include <vector>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "loaders/elf_loader.h"

using namespace rp2040;

namespace {

void wr16(std::vector<std::uint8_t>& b, std::size_t off, std::uint16_t v) {
    b[off] = static_cast<std::uint8_t>(v);
    b[off + 1] = static_cast<std::uint8_t>(v >> 8);
}
void wr32(std::vector<std::uint8_t>& b, std::size_t off, std::uint32_t v) {
    b[off] = static_cast<std::uint8_t>(v);
    b[off + 1] = static_cast<std::uint8_t>(v >> 8);
    b[off + 2] = static_cast<std::uint8_t>(v >> 16);
    b[off + 3] = static_cast<std::uint8_t>(v >> 24);
}

struct Segment {
    std::uint32_t paddr;
    std::vector<std::uint8_t> data;
    std::uint32_t memsz;  // >= data.size(); extra is BSS
};

// Assemble a minimal but valid little-endian EM_ARM ET_EXEC ELF.
std::vector<std::uint8_t> build_elf(std::uint32_t entry, const std::vector<Segment>& segs,
                                    std::uint16_t e_machine = 40 /*EM_ARM*/) {
    constexpr std::size_t kEhdr = 52, kPhdr = 32;
    const std::size_t ph_off = kEhdr;
    std::size_t data_off = ph_off + segs.size() * kPhdr;

    std::vector<std::uint8_t> b(data_off, 0u);

    b[0] = 0x7F; b[1] = 'E'; b[2] = 'L'; b[3] = 'F';
    b[4] = 1;  // ELFCLASS32
    b[5] = 1;  // ELFDATA2LSB
    b[6] = 1;  // EV_CURRENT
    wr16(b, 16, 2);            // e_type = ET_EXEC
    wr16(b, 18, e_machine);    // e_machine
    wr32(b, 20, 1);            // e_version
    wr32(b, 24, entry);        // e_entry
    wr32(b, 28, static_cast<std::uint32_t>(ph_off));  // e_phoff
    wr32(b, 32, 0);            // e_shoff
    wr32(b, 36, 0);            // e_flags
    wr16(b, 40, kEhdr);        // e_ehsize
    wr16(b, 42, kPhdr);        // e_phentsize
    wr16(b, 44, static_cast<std::uint16_t>(segs.size()));  // e_phnum

    for (std::size_t i = 0; i < segs.size(); ++i) {
        const Segment& s = segs[i];
        const std::size_t po = ph_off + i * kPhdr;
        wr32(b, po + 0, 1);                       // p_type = PT_LOAD
        wr32(b, po + 4, static_cast<std::uint32_t>(data_off));  // p_offset
        wr32(b, po + 8, s.paddr);                 // p_vaddr
        wr32(b, po + 12, s.paddr);                // p_paddr
        wr32(b, po + 16, static_cast<std::uint32_t>(s.data.size()));  // p_filesz
        wr32(b, po + 20, s.memsz);                // p_memsz
        wr32(b, po + 24, 0x5);                    // p_flags = R|X
        wr32(b, po + 28, 4);                      // p_align

        b.insert(b.end(), s.data.begin(), s.data.end());
        data_off += s.data.size();
    }
    return b;
}

}  // namespace

TEST_CASE("loads a single code segment and reports the entry point") {
    Memory mem;
    const std::vector<std::uint8_t> code{0x01, 0x20, 0x40, 0x1C};  // movs r0,#1 ; adds r0,r0,#1
    const auto elf = build_elf(0x20000000u | 1u, {{0x20000000u, code, 4}});

    const ElfImage img = load_elf(mem, elf.data(), elf.size());
    REQUIRE(img.ok);
    CHECK(img.entry == 0x20000001u);
    CHECK(img.segments_loaded == 1);
    CHECK(img.lowest_addr == 0x20000000u);
    CHECK(mem.read_word(0x20000000u).value == 0x1C402001u);  // LE: 01 20 40 1C
}

TEST_CASE("zero-fills the BSS tail of a segment") {
    Memory mem;
    REQUIRE(mem.write_word(0x20001000u, 0xFFFFFFFFu) == BusStatus::Ok);
    // filesz 4, memsz 12 -> 8 bytes of BSS after the initialised word
    const auto elf = build_elf(0x20001001u, {{0x20001000u, {0xAA, 0xBB, 0xCC, 0xDD}, 12}});

    REQUIRE(load_elf(mem, elf.data(), elf.size()).ok);
    CHECK(mem.read_word(0x20001000u).value == 0xDDCCBBAAu);
    CHECK(mem.read_word(0x20001004u).value == 0u);  // BSS cleared
    CHECK(mem.read_word(0x20001008u).value == 0u);
}

TEST_CASE("loads a flash code segment plus an SRAM data segment") {
    Memory mem;
    const auto elf = build_elf(0x10000001u, {
        {0x10000000u, std::vector<std::uint8_t>(64, 0x11), 64},   // .text in flash
        {0x20000000u, std::vector<std::uint8_t>(16, 0x22), 32},   // .data + .bss in SRAM
    });
    const ElfImage img = load_elf(mem, elf.data(), elf.size());
    REQUIRE(img.ok);
    CHECK(img.segments_loaded == 2);
    CHECK(img.lowest_addr == 0x10000000u);
    CHECK(img.highest_addr == 0x20000020u);
    CHECK(mem.read_word(0x10000000u).value == 0x11111111u);
    CHECK(mem.read_word(0x20000000u).value == 0x22222222u);
    CHECK(mem.read_word(0x2000001Cu).value == 0u);  // .bss
}

TEST_CASE("rejects malformed images") {
    Memory mem;
    const auto good = build_elf(0x20000001u, {{0x20000000u, {0, 0, 0, 0}, 4}});

    SUBCASE("too small") {
        CHECK_FALSE(load_elf(mem, good.data(), 10).ok);
    }
    SUBCASE("bad magic") {
        auto bad = good;
        bad[1] = 'X';
        CHECK_FALSE(load_elf(mem, bad.data(), bad.size()).ok);
    }
    SUBCASE("not EM_ARM") {
        const auto x86 = build_elf(0x20000001u, {{0x20000000u, {0, 0, 0, 0}, 4}}, 62 /*EM_X86_64*/);
        const ElfImage img = load_elf(mem, x86.data(), x86.size());
        CHECK_FALSE(img.ok);
        CHECK(img.error.find("EM_ARM") != std::string::npos);
    }
    SUBCASE("segment address not backed by memory") {
        const auto bad = build_elf(0x30000001u, {{0x30000000u, {1, 2, 3, 4}, 4}});
        CHECK_FALSE(load_elf(mem, bad.data(), bad.size()).ok);
    }
    SUBCASE("segment file range past end of file") {
        auto bad = good;
        wr32(bad, 52 + 16, 0x10000u);  // p_filesz absurdly large
        CHECK_FALSE(load_elf(mem, bad.data(), bad.size()).ok);
    }
}

TEST_CASE("loaded image runs on the CPU") {
    Memory mem;
    RegisterFile regs;
    Cpu cpu(regs, mem);

    // r0 = 4; do { r1 += r0; } while (--r0); then spin.
    const std::vector<std::uint8_t> prog{
        0x04, 0x20,  // movs r0, #4
        0x00, 0x21,  // movs r1, #0
        0x09, 0x18,  // adds r1, r1, r0
        0x01, 0x38,  // subs r0, #1
        0xFC, 0xD1,  // bne  -8
        0xFE, 0xE7,  // b .
    };
    const auto elf = build_elf(0x20000000u | 1u, {{0x20000000u, prog, static_cast<std::uint32_t>(prog.size())}});
    const ElfImage img = load_elf(mem, elf.data(), elf.size());
    REQUIRE(img.ok);

    regs.set_pc(img.entry & ~1u);
    for (int i = 0; i < 100 && regs.pc() != 0x2000000Au; ++i) {
        REQUIRE(cpu.step() == ExecStatus::Ok);
    }
    CHECK(regs.get(1) == 10);   // 4 + 3 + 2 + 1
    CHECK(regs.get(0) == 0);
}
