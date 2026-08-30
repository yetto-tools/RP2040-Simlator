// Unit tests for the pioasm-language assembler (BACKLOG P7.3).
#include "doctest.h"

#include <cstdint>
#include <string>

#include "pio/pio_assembler.h"
#include "pio_isa.h"

using namespace rp2040;

namespace {
PioAssembly asm_ok(const std::string& src) {
    const PioAssembly a = assemble_pio(src);
    REQUIRE_MESSAGE(a.ok, a.error);
    return a;
}
}  // namespace

TEST_CASE("squarewave: directives, wrap and the delay suffix") {
    const auto a = asm_ok(R"(
        .program squarewave
            set pindirs, 1
        .wrap_target
            set pins, 1 [1]
            set pins, 0 [1]
        .wrap
    )");

    CHECK(a.program_name == "squarewave");
    REQUIRE(a.instructions.size() == 3);
    CHECK(a.instructions[0] == 0xE081u);  // set pindirs, 1
    CHECK(a.instructions[1] == 0xE101u);  // set pins, 1 [1]
    CHECK(a.instructions[2] == 0xE100u);  // set pins, 0 [1]
    CHECK(a.wrap_target == 1);
    CHECK(a.wrap == 2);
}

TEST_CASE("labels resolve as jmp targets, forward and backward") {
    const auto a = asm_ok(R"(
        .program loops
        top:
            jmp x-- top
            jmp !y done
            jmp top
        done:
            nop
    )");
    REQUIRE(a.instructions.size() == 4);

    const PioInstr j0 = pio_decode(a.instructions[0]);
    CHECK(j0.op == PioOp::JMP);
    CHECK(j0.condition == kJmpXDec);
    CHECK(j0.address == 0);

    const PioInstr j1 = pio_decode(a.instructions[1]);
    CHECK(j1.condition == kJmpNotY);
    CHECK(j1.address == 3);          // "done"

    CHECK(pio_decode(a.instructions[2]).address == 0);
    CHECK(a.instructions[3] == 0xA042u);  // nop == mov y, y
}

TEST_CASE(".side_set splits the delay/side-set field") {
    const auto a = asm_ok(R"(
        .program ws
        .side_set 1
        .wrap_target
        bitloop:
            out x, 1        side 0 [2]
            jmp !x is_zero  side 1 [1]
            jmp bitloop     side 1 [3]
        is_zero:
            nop             side 0 [3]
        .wrap
    )");

    CHECK(a.side_set_count == 1);
    CHECK_FALSE(a.side_set_opt);
    REQUIRE(a.instructions.size() == 4);

    // out x, 1 side 0 [2] : body 0x6021, field = (0<<4)|2 -> word 0x6221
    CHECK(a.instructions[0] == 0x6221u);
    const PioInstr o = pio_decode(a.instructions[0]);
    CHECK(o.op == PioOp::OUT);
    CHECK(o.destination == kOutX);
    CHECK(o.bit_count == 1);
    CHECK(o.delay_sideset == 0x02u);  // side 0, delay 2

    // jmp !x is_zero side 1 [1] : is_zero == index 3, body 0x0023,
    //   field = enable? none (not opt) -> (1<<4)|1 = 0x11 -> word 0x1123
    CHECK(a.instructions[1] == 0x1123u);
    CHECK(pio_decode(a.instructions[1]).delay_sideset == 0x11u);

    CHECK(a.instructions[3] == 0xA342u);  // nop side 0 [3] : field 0x03
}

TEST_CASE("optional side-set reserves the enable bit") {
    const auto a = asm_ok(R"(
        .program opt
        .side_set 2 opt
            set pins, 3 side 1 [1]
            set pins, 0
    )");
    CHECK(a.side_set_count == 2);
    CHECK(a.side_set_opt);

    // total side-set bits = 3, delay_bits = 2.
    // set pins,3 : body 0xE003. side 1 -> enable (1<<4) | (1<<2) ; delay 1.
    // field = 0x10 | 0x04 | 0x01 = 0x15 -> word 0xE003 | 0x1500 = 0xF503
    CHECK(a.instructions[0] == 0xF503u);
    // set pins,0 with no 'side' : enable bit stays 0, field 0 -> 0xE000
    CHECK(a.instructions[1] == 0xE000u);
}

TEST_CASE(".define constants and simple expressions") {
    const auto a = asm_ok(R"(
        .program defs
        .define PUBLIC BIT 4
        .define STEP (BIT - 1)
            set x, BIT
            out pins, STEP
    )");
    REQUIRE(a.instructions.size() == 2);
    CHECK((a.instructions[0] & 0x1Fu) == 4u);          // set x, 4
    CHECK(pio_decode(a.instructions[1]).bit_count == 3);  // out pins, 3
    CHECK(a.public_defines.at("BIT") == 4);
}

TEST_CASE("mov operators and every irq form") {
    const auto a = asm_ok(R"(
        .program m
            mov x, ~null
            mov osr, ::isr
            irq set 2
            irq wait 3
            irq clear 4
            wait 1 irq 5 rel
    )");
    const PioInstr m0 = pio_decode(a.instructions[0]);
    CHECK(m0.op == PioOp::MOV);
    CHECK(m0.mov_op == kMovInvert);
    CHECK(m0.source == kMovNull);

    CHECK(pio_decode(a.instructions[1]).mov_op == kMovBitRev);

    const PioInstr set = pio_decode(a.instructions[2]);
    CHECK(set.op == PioOp::IRQ);
    CHECK_FALSE(set.clear);
    CHECK_FALSE(set.wait);
    CHECK(set.index == 2);

    CHECK(pio_decode(a.instructions[3]).wait);       // irq wait 3
    CHECK(pio_decode(a.instructions[4]).clear);      // irq clear 4

    const PioInstr w = pio_decode(a.instructions[5]);
    CHECK(w.op == PioOp::WAIT);
    CHECK(w.source == kWaitIrq);
    CHECK(w.index == (5u | 0x10u));                  // rel sets bit 4
}

TEST_CASE("push / pull option flags") {
    const auto a = asm_ok(R"(
        .program pp
            push iffull block
            pull ifempty noblock
            push noblock
    )");
    const PioInstr p0 = pio_decode(a.instructions[0]);
    CHECK(p0.op == PioOp::PUSH);
    CHECK(p0.if_full);
    CHECK(p0.block);

    const PioInstr p1 = pio_decode(a.instructions[1]);
    CHECK(p1.op == PioOp::PULL);
    CHECK(p1.if_empty);
    CHECK_FALSE(p1.block);

    CHECK_FALSE(pio_decode(a.instructions[2]).block);
}

TEST_CASE("comments in every supported style are ignored") {
    const auto a = asm_ok(
        "; header\n"
        ".program c   // trailing\n"
        "/* a\n"
        "   multi-line\n"
        "   block */\n"
        "  set x, 1  ; inline\n");
    CHECK(a.instructions.size() == 1);
}

TEST_CASE("diagnostics point at the offending line") {
    SUBCASE("unknown mnemonic") {
        const PioAssembly a = assemble_pio(".program e\n  frobnicate x\n");
        CHECK_FALSE(a.ok);
        CHECK(a.error.find("line 2") != std::string::npos);
    }
    SUBCASE("jmp target out of range") {
        CHECK_FALSE(assemble_pio(".program e\n set x,1\n jmp 40\n").ok);
    }
    SUBCASE("undefined symbol") {
        const PioAssembly a = assemble_pio(".program e\n set x, NOPE\n");
        CHECK_FALSE(a.ok);
        CHECK(a.error.find("NOPE") != std::string::npos);
    }
    SUBCASE("side used without .side_set") {
        CHECK_FALSE(assemble_pio(".program e\n set x, 1 side 1\n").ok);
    }
    SUBCASE("delay too large for the side-set width") {
        CHECK_FALSE(assemble_pio(".program e\n.side_set 3\n set x,1 side 0 [8]\n").ok);
    }
    SUBCASE("empty program") {
        CHECK_FALSE(assemble_pio(".program e\n.define X 1\n").ok);
    }
}
