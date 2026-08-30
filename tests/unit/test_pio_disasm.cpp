// Unit tests for the PIO disassembler (pio_disasm) - checked by round-tripping
// through the assembler.
#include "doctest.h"

#include <cstdint>
#include <string>

#include "pio/pio_assembler.h"
#include "pio/pio_disasm.h"

using namespace rp2040;

TEST_CASE("individual instructions render as pioasm text") {
    CHECK(pio_disassemble(0xE081u) == "set pindirs, 1");
    CHECK(pio_disassemble(0x0044u) == "jmp x-- 4");
    CHECK(pio_disassemble(0x6021u) == "out x, 1");
    CHECK(pio_disassemble(0x4004u) == "in pins, 4");
    CHECK(pio_disassemble(0xA042u) == "mov y, y");
    CHECK(pio_disassemble(0xA0EBu) == "mov osr, ~null");
    CHECK(pio_disassemble(0x2085u) == "wait 1 gpio 5");
    CHECK(pio_disassemble(0xC003u) == "irq set 3");
    CHECK(pio_disassemble(0x8060u) == "push iffull block");
}

TEST_CASE("delay and side-set split out of the [12:8] field") {
    CHECK(pio_disassemble(0xE101u) == "set pins, 1 [1]");
    // side-set 1 bit, no opt: word 0x6221 = out x,1 side 0 [2]
    CHECK(pio_disassemble(0x6221u, 1, false) == "out x, 1 side 0 [2]");
    CHECK(pio_disassemble(0x1123u, 1, false) == "jmp !x 3 side 1 [1]");
    // optional side-set: 0xF503 = set pins,3 side 1 [1] with .side_set 2 opt
    CHECK(pio_disassemble(0xF503u, 2, true) == "set pins, 3 side 1 [1]");
    CHECK(pio_disassemble(0xE000u, 2, true) == "set pins, 0");  // opt enable clear
}

TEST_CASE("assemble -> disassemble -> assemble is a fixed point") {
    const char* src = R"(
        .program rt
        .side_set 1
        top:
            out x, 1        side 0 [2]
            jmp !x is_zero  side 1 [1]
            jmp top         side 1 [3]
        is_zero:
            nop             side 0
    )";
    const PioAssembly a = assemble_pio(src);
    REQUIRE(a.ok);

    std::string round;
    round += ".program rt2\n.side_set 1\n";
    for (std::uint16_t w : a.instructions) {
        round += pio_disassemble(w, a.side_set_count, a.side_set_opt) + "\n";
    }
    const PioAssembly b = assemble_pio(round);
    REQUIRE_MESSAGE(b.ok, b.error);
    REQUIRE(b.instructions.size() == a.instructions.size());
    for (std::size_t i = 0; i < a.instructions.size(); ++i) {
        CHECK(b.instructions[i] == a.instructions[i]);
    }
}
