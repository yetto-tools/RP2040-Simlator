// Unit tests for DebugSession, the JSON-friendly debug engine behind
// rp2040-lab-server (tools/lab_server). Its run/step/breakpoint logic is a
// near-copy of the proven pattern in src/debuggers/gdb_stub.cpp's run();
// these tests exercise the same behaviour through DebugSession's API,
// plus the background-thread run/pause mechanics that are new here.
#include "doctest.h"

#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "debug_session.h"

using namespace rp2040lab;

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

// A minimal but valid little-endian EM_ARM ET_EXEC ELF with one PT_LOAD
// segment - same shape as tests/unit/test_elf_loader.cpp's own helper,
// duplicated locally rather than shared (matching this test suite's
// existing per-file fixture convention).
std::vector<std::uint8_t> build_elf(std::uint32_t entry, std::uint32_t paddr,
                                     const std::vector<std::uint8_t>& code) {
    constexpr std::size_t kEhdr = 52, kPhdr = 32;
    std::vector<std::uint8_t> b(kEhdr + kPhdr, 0u);

    b[0] = 0x7F; b[1] = 'E'; b[2] = 'L'; b[3] = 'F';
    b[4] = 1;  // ELFCLASS32
    b[5] = 1;  // ELFDATA2LSB
    b[6] = 1;  // EV_CURRENT
    wr16(b, 16, 2);   // e_type = ET_EXEC
    wr16(b, 18, 40);  // e_machine = EM_ARM
    wr32(b, 20, 1);   // e_version
    wr32(b, 24, entry);
    wr32(b, 28, kEhdr);  // e_phoff
    wr16(b, 40, kEhdr);  // e_ehsize
    wr16(b, 42, kPhdr);  // e_phentsize
    wr16(b, 44, 1);      // e_phnum

    wr32(b, kEhdr + 0, 1);                                  // p_type = PT_LOAD
    wr32(b, kEhdr + 4, static_cast<std::uint32_t>(kEhdr + kPhdr));  // p_offset
    wr32(b, kEhdr + 8, paddr);
    wr32(b, kEhdr + 12, paddr);
    wr32(b, kEhdr + 16, static_cast<std::uint32_t>(code.size()));  // p_filesz
    wr32(b, kEhdr + 20, static_cast<std::uint32_t>(code.size()));  // p_memsz
    wr32(b, kEhdr + 24, 0x5);  // p_flags = R|X
    wr32(b, kEhdr + 28, 4);

    b.insert(b.end(), code.begin(), code.end());
    return b;
}

constexpr std::uint32_t kBase = 0x20000000u;

// movs r0,#1 ; movs r0,#2 ; movs r0,#3 ; b . (infinite spin)
std::vector<std::uint8_t> spin_program() {
    std::vector<std::uint8_t> code(8);
    wr16(code, 0, 0x2001);
    wr16(code, 2, 0x2002);
    wr16(code, 4, 0x2003);
    wr16(code, 6, 0xE7FE);
    return code;
}

bool wait_for(DebugSession& s, RunStatus target, int max_ms = 2000) {
    for (int i = 0; i < max_ms; i += 5) {
        if (s.snapshot().status == target) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

// movs r0,#1 ; bkpt #0 ; movs r0,#0xFF (should never run if the bkpt halts)
std::vector<std::uint8_t> bkpt_program() {
    std::vector<std::uint8_t> code(6);
    wr16(code, 0, 0x2001);
    wr16(code, 2, 0xBE00);
    wr16(code, 4, 0x20FF);
    return code;
}

}  // namespace

TEST_CASE("load() reports a loaded, halted snapshot at the entry point") {
    DebugSession s;
    const auto elf = build_elf(kBase | 1u, kBase, spin_program());
    std::string error;
    REQUIRE(s.load(elf, "elf", /*from_entry=*/true, error));

    const StateSnapshot snap = s.snapshot();
    CHECK(snap.loaded);
    CHECK(snap.status == RunStatus::Halted);
    CHECK(snap.pc == kBase);
    CHECK(snap.gpio.size() == 30u);
}

TEST_CASE("load() reports the error on a malformed image") {
    DebugSession s;
    std::string error;
    const std::vector<std::uint8_t> garbage{0, 1, 2, 3};
    CHECK_FALSE(s.load(garbage, "elf", true, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(s.snapshot().loaded);
}

TEST_CASE("step() advances exactly one instruction") {
    DebugSession s;
    const auto elf = build_elf(kBase | 1u, kBase, spin_program());
    std::string error;
    REQUIRE(s.load(elf, "elf", true, error));

    s.step();
    const StateSnapshot snap = s.snapshot();
    CHECK(snap.pc == kBase + 2);
    CHECK(snap.r[0] == 1u);
    CHECK(snap.status == RunStatus::Halted);
}

TEST_CASE("a breakpoint stops a background run there") {
    DebugSession s;
    const auto elf = build_elf(kBase | 1u, kBase, spin_program());
    std::string error;
    REQUIRE(s.load(elf, "elf", true, error));

    s.add_breakpoint(kBase + 4);
    s.start_run();
    REQUIRE(wait_for(s, RunStatus::Breakpoint));

    const StateSnapshot snap = s.snapshot();
    CHECK(snap.pc == kBase + 4);
    CHECK(snap.r[0] == 2u);  // the second movs already ran
}

TEST_CASE("pause() stops a spinning background run") {
    DebugSession s;
    const auto elf = build_elf(kBase | 1u, kBase, spin_program());
    std::string error;
    REQUIRE(s.load(elf, "elf", true, error));

    s.start_run();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    s.pause();

    const StateSnapshot snap = s.snapshot();
    CHECK(snap.status == RunStatus::Halted);
    CHECK(snap.cycles > 0u);
}

TEST_CASE("remove_breakpoint() lets a run pass through") {
    DebugSession s;
    const auto elf = build_elf(kBase | 1u, kBase, spin_program());
    std::string error;
    REQUIRE(s.load(elf, "elf", true, error));

    s.add_breakpoint(kBase + 4);
    s.remove_breakpoint(kBase + 4);
    CHECK(s.breakpoints().empty());

    s.start_run();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    s.pause();

    // Without the breakpoint the program reaches its spin loop and keeps
    // running past address kBase+4 (many cycles, not stopped there).
    CHECK(s.snapshot().cycles > 4u);
}

// A real `bkpt` instruction in the executed code (not one of this session's
// own address-matched breakpoints_) must still stop the run - found via a
// real pico-sdk boot silently running past its default "unhandled ISR" bkpt
// stubs into whatever data followed them (BACKLOG.md P10.3).
TEST_CASE("a live bkpt instruction halts a background run") {
    DebugSession s;
    const auto elf = build_elf(kBase | 1u, kBase, bkpt_program());
    std::string error;
    REQUIRE(s.load(elf, "elf", true, error));

    s.start_run();
    REQUIRE(wait_for(s, RunStatus::Breakpoint));

    const StateSnapshot snap = s.snapshot();
    CHECK(snap.pc == kBase + 4);  // just past the bkpt
    CHECK(snap.r[0] == 1u);       // the trailing movs r0,#0xFF never ran
}
