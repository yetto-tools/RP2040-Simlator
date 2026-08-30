// Unit tests for the PIO debugger (breakpoints, stepping, inspection, trace).
#include "doctest.h"

#include <cstdint>

#include "debuggers/pio_debugger.h"
#include "peripherals/gpio.h"
#include "pio/pio_assembler.h"
#include "pio/pio_block.h"

using namespace rp2040;

namespace {

void load(PioBlock& pio, unsigned sm, const PioAssembly& a) {
    for (std::size_t i = 0; i < a.instructions.size(); ++i) {
        pio.write_instruction(static_cast<unsigned>(i), a.instructions[i]);
    }
    pio.sm(sm).cfg.wrap_bottom = static_cast<std::uint8_t>(a.wrap_target);
    pio.sm(sm).cfg.wrap_top = static_cast<std::uint8_t>(a.wrap);
    pio.sm(sm).set_enabled(true);
    pio.sm(sm).restart();
}

}  // namespace

TEST_CASE("run stops when an SM is about to execute a breakpoint address") {
    Gpio gpio;
    PioBlock pio0(gpio, 0), pio1(gpio, 1);
    // set x,2 ; loop: jmp x-- loop ; spin: jmp spin
    const PioAssembly a = assemble_pio(R"(
        .program t
            set x, 2
        loop:
            jmp x-- loop
        spin:
            jmp spin
    )");
    REQUIRE(a.ok);
    load(pio0, 0, a);

    PioDebugger dbg(pio0, pio1);
    dbg.add_breakpoint(0, 0, 2);   // the "spin" instruction

    const PioDebugger::Hit h = dbg.run(50);
    REQUIRE(h.hit);
    CHECK(h.block == 0);
    CHECK(h.sm == 0);
    CHECK(h.pc == 2);
    // x counted 2 -> 1 -> 0, then the final x-- underflowed as the jmp fell through
    CHECK(dbg.inspect(0, 0).x == 0xFFFFFFFFu);
}

TEST_CASE("inspect reports registers, FIFO levels and disassembly") {
    Gpio gpio;
    PioBlock pio0(gpio, 0), pio1(gpio, 1);
    const PioAssembly a = assemble_pio(R"(
        .program t
            set y, 5
            in y, 32
            push block
        spin:
            jmp spin
    )");
    REQUIRE(a.ok);
    pio0.sm(0).cfg.in_shiftdir_right = false;
    load(pio0, 0, a);

    PioDebugger dbg(pio0, pio1);
    dbg.run(10);

    const PioDebugger::SmSnapshot s = dbg.inspect(0, 0);
    CHECK(s.enabled);
    CHECK(s.y == 5u);
    CHECK(s.rx_level == 1u);
    CHECK(s.instructions_retired >= 3u);
    CHECK(s.disasm == "jmp 3");        // sitting on "spin: jmp spin"
}

TEST_CASE("instruction trace records each retired SM instruction") {
    Gpio gpio;
    PioBlock pio0(gpio, 0), pio1(gpio, 1);
    const PioAssembly a = assemble_pio(R"(
        .program t
            set x, 1
        spin:
            jmp spin
    )");
    REQUIRE(a.ok);
    load(pio0, 0, a);

    PioDebugger dbg(pio0, pio1);
    dbg.set_trace(true);
    dbg.step();   // set x, 1
    dbg.step();   // jmp spin
    dbg.step();   // jmp spin

    REQUIRE(dbg.trace().size() == 3);
    CHECK(dbg.trace()[0].pc == 0);
    CHECK(dbg.trace()[0].instr == a.instructions[0]);
    CHECK(dbg.trace()[1].pc == 1);
    CHECK(dbg.trace()[2].pc == 1);
    CHECK(dbg.trace()[2].cycle == 3u);
}
