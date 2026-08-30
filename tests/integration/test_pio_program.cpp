// Integration test: assemble real pioasm source with the built-in assembler
// (P7.3) and run it on the PIO engine (P2) driving the GPIO model (P3),
// end to end. No external toolchain required.
#include "doctest.h"

#include <cstdint>
#include <string>

#include "peripherals/gpio.h"
#include "pio/pio_assembler.h"
#include "pio/pio_block.h"
#include "pio/state_machine.h"

using namespace rp2040;

namespace {

// Load an assembled program into the block's instruction memory at `origin`
// and copy its wrap window onto one state machine.
void load(PioBlock& pio, unsigned sm_index, const PioAssembly& prog, unsigned origin = 0) {
    REQUIRE(prog.ok);
    for (std::size_t i = 0; i < prog.instructions.size(); ++i) {
        pio.write_instruction(origin + static_cast<unsigned>(i), prog.instructions[i]);
    }
    StateMachine& sm = pio.sm(sm_index);
    sm.cfg.wrap_bottom = static_cast<std::uint8_t>(origin + prog.wrap_target);
    sm.cfg.wrap_top = static_cast<std::uint8_t>(origin + prog.wrap);
}

}  // namespace

TEST_CASE("assembled square-wave program toggles the LED pin") {
    const PioAssembly prog = assemble_pio(R"(
        .program squarewave
        .wrap_target
            set pins, 1
            set pins, 0
        .wrap
    )");
    REQUIRE_MESSAGE(prog.ok, prog.error);

    Gpio gpio;
    PioBlock pio(gpio, 0);
    gpio.set_funcsel(25, Gpio::kFuncPio0);
    gpio.driver_set_pindir(Gpio::kPio0, 25, true);

    StateMachine& sm = pio.sm(0);
    sm.cfg.set_base = 25;
    sm.cfg.set_count = 1;
    load(pio, 0, prog);
    sm.set_enabled(true);
    sm.restart();

    pio.tick(); CHECK(gpio.level(25));
    pio.tick(); CHECK_FALSE(gpio.level(25));
    pio.tick(); CHECK(gpio.level(25));
    pio.tick(); CHECK_FALSE(gpio.level(25));
}

TEST_CASE("assembled side-set + delay program produces the expected pin timing") {
    // One side-set bit on pin 10; each instruction also idles for its delay.
    const PioAssembly prog = assemble_pio(R"(
        .program blink
        .side_set 1
        .wrap_target
            nop side 1 [1]
            nop side 0 [1]
        .wrap
    )");
    REQUIRE_MESSAGE(prog.ok, prog.error);
    CHECK(prog.side_set_count == 1);

    Gpio gpio;
    PioBlock pio(gpio, 0);
    gpio.set_funcsel(10, Gpio::kFuncPio0);
    gpio.driver_set_pindir(Gpio::kPio0, 10, true);

    StateMachine& sm = pio.sm(0);
    sm.cfg.sideset_count = 1;
    sm.cfg.sideset_base = 10;
    load(pio, 0, prog);
    sm.set_enabled(true);
    sm.restart();

    pio.tick(); CHECK(gpio.level(10));        // exec nop, side 1
    pio.tick(); CHECK(gpio.level(10));        // delay slot, pin holds
    pio.tick(); CHECK_FALSE(gpio.level(10));  // exec nop, side 0
    pio.tick(); CHECK_FALSE(gpio.level(10));  // delay slot
    pio.tick(); CHECK(gpio.level(10));        // wrapped back to instruction 0
}

TEST_CASE("assembled loop with labels and x-- counts iterations into the RX FIFO") {
    // Push a decreasing counter: set x, 3 ; loop: in x,32 ; push ; jmp x-- loop
    const PioAssembly prog = assemble_pio(R"(
        .program count
            set x, 3
        loop:
            in x, 32
            push block
            jmp x-- loop
        spin:
            jmp spin
    )");
    REQUIRE_MESSAGE(prog.ok, prog.error);

    Gpio gpio;
    PioBlock pio(gpio, 0);
    StateMachine& sm = pio.sm(0);
    sm.cfg.in_shiftdir_right = false;
    sm.cfg.wrap_bottom = 0;
    sm.cfg.wrap_top = static_cast<std::uint8_t>(prog.instructions.size() - 1);
    load(pio, 0, prog);
    sm.set_enabled(true);
    sm.restart();

    // Run enough ticks for the four (x = 3,2,1,0) iterations to complete.
    for (int i = 0; i < 40; ++i) pio.tick();

    REQUIRE(sm.rx.level() == 4);
    std::uint32_t v = 0;
    REQUIRE(sm.rx.pop(v)); CHECK(v == 3u);
    REQUIRE(sm.rx.pop(v)); CHECK(v == 2u);
    REQUIRE(sm.rx.pop(v)); CHECK(v == 1u);
    REQUIRE(sm.rx.pop(v)); CHECK(v == 0u);
}
