// Unit tests for the PIO state-machine datapath (datasheet 3.4, 3.5).
#include "doctest.h"

#include <array>
#include <cstdint>

#include "pio/state_machine.h"
#include "pio_isa.h"

using namespace rp2040;

namespace {

// A state machine wired to a 32-word program, enabled, restarted.
struct SmFix {
    std::array<std::uint16_t, 32> prog{};
    StateMachine sm;

    SmFix() {
        sm.set_program(prog.data());
        sm.set_enabled(true);
        sm.restart();
    }
    // run one instruction (skipping the delay slots it may insert)
    void step_instr() {
        for (int i = 0; i < 64; ++i) {
            const auto o = sm.tick();
            if (o.executed || o.stalled) return;
        }
        FAIL("state machine made no progress");
    }
};

}  // namespace

TEST_CASE_FIXTURE(SmFix, "SET writes X/Y and advances PC with wrap") {
    prog[0] = 0xE03F;  // set x, 31
    prog[1] = 0xE041;  // set y, 1
    step_instr();
    CHECK(sm.x == 31);
    CHECK(sm.pc == 1);
    step_instr();
    CHECK(sm.y == 1);
    CHECK(sm.pc == 2);
}

TEST_CASE_FIXTURE(SmFix, "wrap sends PC from wrap_top back to wrap_bottom") {
    sm.cfg.wrap_top = 2;
    sm.cfg.wrap_bottom = 1;
    sm.restart();
    CHECK(sm.pc == 1);
    prog[1] = 0xE021;  // set x, 1
    prog[2] = 0xE022;  // set x, 2
    step_instr();  CHECK(sm.pc == 2);
    step_instr();  CHECK(sm.pc == 1);   // wrapped
    CHECK(sm.x == 2);
}

TEST_CASE_FIXTURE(SmFix, "JMP conditions") {
    SUBCASE("jmp always") {
        prog[0] = 0x0005;  // jmp 5
        step_instr();
        CHECK(sm.pc == 5);
    }
    SUBCASE("jmp !x taken when X == 0, else falls through") {
        prog[0] = 0x0023;  // jmp !x, 3   (cond 001)
        sm.x = 0;
        step_instr();
        CHECK(sm.pc == 3);
        sm.restart();
        sm.x = 7;
        step_instr();
        CHECK(sm.pc == 1);  // not taken
    }
    SUBCASE("jmp x-- decrements X unconditionally, branches while non-zero") {
        prog[0] = 0x0040;  // jmp x--, 0   (cond 010)
        sm.x = 2;
        step_instr();  CHECK(sm.pc == 0);  CHECK(sm.x == 1);
        step_instr();  CHECK(sm.pc == 0);  CHECK(sm.x == 0);
        step_instr();  CHECK(sm.pc == 1);  CHECK(sm.x == 0xFFFFFFFFu);  // wrapped, not taken
    }
    SUBCASE("jmp x!=y") {
        prog[0] = 0x00A4;  // jmp x!=y, 4  (cond 101)
        sm.x = 1; sm.y = 2;
        step_instr();  CHECK(sm.pc == 4);
        sm.restart(); sm.x = 5; sm.y = 5;
        step_instr();  CHECK(sm.pc == 1);
    }
}

TEST_CASE_FIXTURE(SmFix, "MOV with none / invert / bit-reverse") {
    sm.osr = 0x00000001u;
    prog[0] = 0xA027;  // mov x, osr
    step_instr();
    CHECK(sm.x == 1);

    sm.restart();
    sm.osr = 0x0000000Fu;
    prog[0] = 0xA02F;  // mov x, ~osr
    step_instr();
    CHECK(sm.x == 0xFFFFFFF0u);

    sm.restart();
    sm.osr = 0x00000001u;
    prog[0] = 0xA037;  // mov x, ::osr  (bit-reverse)
    step_instr();
    CHECK(sm.x == 0x80000000u);
}

TEST_CASE_FIXTURE(SmFix, "OUT shifts right out of the OSR (LSB first)") {
    sm.cfg.out_shiftdir_right = true;
    sm.osr = 0xDEADBEEFu;
    sm.osr_shift_count = 0;
    prog[0] = 0x6028;  // out x, 8
    step_instr();
    CHECK(sm.x == 0xEF);
    CHECK(sm.osr == 0x00DEADBEu);
    CHECK(sm.osr_shift_count == 8);
}

TEST_CASE_FIXTURE(SmFix, "OUT shifts left out of the OSR (MSB first)") {
    sm.cfg.out_shiftdir_right = false;
    sm.osr = 0xDEADBEEFu;
    sm.osr_shift_count = 0;
    prog[0] = 0x6028;  // out x, 8
    step_instr();
    CHECK(sm.x == 0xDE);
    CHECK(sm.osr == 0xADBEEF00u);
}

TEST_CASE_FIXTURE(SmFix, "IN shifts into the ISR from the configured end") {
    sm.cfg.in_shiftdir_right = true;
    sm.isr = 0;
    prog[0] = 0x4021;  // in x, 1   (source 001, count 1)
    sm.x = 1;
    step_instr();
    CHECK(sm.isr == 0x80000000u);   // 1 bit entered at the MSB
    CHECK(sm.isr_shift_count == 1);

    sm.restart();
    sm.cfg.in_shiftdir_right = false;
    sm.x = 0xFF;
    prog[0] = 0x4028;  // in x, 8
    step_instr();
    CHECK(sm.isr == 0xFF);
}

TEST_CASE_FIXTURE(SmFix, "PULL blocks on an empty TX FIFO, then completes") {
    prog[0] = 0x80A0;  // pull block
    auto o = sm.tick();
    CHECK(o.stalled);
    CHECK(sm.pc == 0);        // did not advance

    sm.tx.push(0x12345678u);
    o = sm.tick();
    CHECK(o.executed);
    CHECK(sm.osr == 0x12345678u);
    CHECK(sm.osr_shift_count == 0);
    CHECK(sm.pc == 1);
}

TEST_CASE_FIXTURE(SmFix, "non-blocking PULL on empty loads OSR from X") {
    sm.x = 0xCAFED00Du;
    prog[0] = 0x8080;  // pull noblock
    step_instr();
    CHECK(sm.osr == 0xCAFED00Du);
    CHECK(sm.pc == 1);
}

TEST_CASE_FIXTURE(SmFix, "PUSH moves the ISR into the RX FIFO and clears it") {
    sm.isr = 0xABCD;
    sm.isr_shift_count = 16;
    prog[0] = 0x8020;  // push block
    step_instr();
    std::uint32_t v = 0;
    REQUIRE(sm.rx.pop(v));
    CHECK(v == 0xABCD);
    CHECK(sm.isr == 0);
    CHECK(sm.isr_shift_count == 0);
}

TEST_CASE_FIXTURE(SmFix, "PUSH block stalls when the RX FIFO is full") {
    for (int i = 0; i < 4; ++i) REQUIRE(sm.rx.push(static_cast<std::uint32_t>(i)));
    prog[0] = 0x8020;  // push block
    auto o = sm.tick();
    CHECK(o.stalled);
    CHECK(sm.pc == 0);
}

TEST_CASE_FIXTURE(SmFix, "autopull refills the OSR from TX when it empties") {
    sm.cfg.autopull = true;
    sm.cfg.pull_threshold = 32;
    sm.cfg.out_shiftdir_right = true;
    sm.tx.push(0x11223344u);
    sm.tx.push(0x55667788u);

    prog[0] = 0x6040;  // out y, 32
    prog[1] = 0x6040;  // out y, 32
    step_instr();                       // proactive autopull loads 0x11223344, OUT drains it
    CHECK(sm.y == 0x11223344u);
    step_instr();                       // next OUT: autopull loads 0x55667788
    CHECK(sm.y == 0x55667788u);
}

TEST_CASE_FIXTURE(SmFix, "autopull stalls a program when the TX FIFO runs dry") {
    sm.cfg.autopull = true;
    prog[0] = 0x6040;  // out y, 32
    prog[1] = 0x0001;  // jmp 1
    // no TX data
    auto o = sm.tick();
    CHECK(o.stalled);
    CHECK(sm.pc == 0);
}

TEST_CASE_FIXTURE(SmFix, "autopush moves the ISR to RX at the threshold") {
    sm.cfg.autopush = true;
    sm.cfg.push_threshold = 8;
    sm.cfg.in_shiftdir_right = true;
    sm.x = 0xFF;

    prog[0] = 0x4028;  // in x, 8
    step_instr();
    std::uint32_t v = 0;
    REQUIRE(sm.rx.pop(v));
    CHECK(v == 0xFF000000u);  // 8 bits shifted in at the MSB, then pushed
    CHECK(sm.isr == 0);
    CHECK(sm.isr_shift_count == 0);
}

TEST_CASE_FIXTURE(SmFix, "the delay field idles the SM for N extra cycles") {
    prog[0] = 0x0301;  // jmp 1, [3]   -> delay 3
    prog[1] = 0xE021;  // set x, 1
    auto o = sm.tick();
    CHECK(o.executed);
    CHECK(sm.pc == 1);
    for (int i = 0; i < 3; ++i) {
        o = sm.tick();
        CHECK(o.delayed);
        CHECK(sm.x == 0);   // set has not run yet
    }
    o = sm.tick();
    CHECK(o.executed);
    CHECK(sm.x == 1);
}

TEST_CASE_FIXTURE(SmFix, "MOV x, status reflects TX/RX FIFO level vs STATUS_N") {
    prog[0] = 0xA025;  // mov x, status
    sm.cfg.status_sel_rx = false;  // TXLEVEL
    sm.cfg.status_n = 2;

    // tx level 0 < 2 -> all-ones
    step_instr();
    CHECK(sm.x == 0xFFFFFFFFu);

    // tx level 2, not < 2 -> all-zeros
    sm.restart();
    sm.tx.push(1);
    sm.tx.push(2);
    step_instr();
    CHECK(sm.x == 0u);

    // RXLEVEL selector
    sm.restart();
    sm.cfg.status_sel_rx = true;
    sm.rx.push(1);
    step_instr();  // rx level 1 < 2 -> all-ones
    CHECK(sm.x == 0xFFFFFFFFu);
}

TEST_CASE_FIXTURE(SmFix, "OUT EXEC injects the shifted-out word as an instruction") {
    prog[0] = 0x60F0;  // out exec, 16
    sm.osr = 0x0000E025u;  // encodes "set x, 5"
    sm.osr_shift_count = 0;
    step_instr();
    CHECK(sm.x == 5u);
    CHECK(sm.pc == 1u);  // PC moved once, for the whole OUT EXEC + injected slot
}

TEST_CASE_FIXTURE(SmFix, "MOV EXEC injects the moved word as an instruction") {
    prog[0] = 0xA081;  // mov exec, x
    sm.x = 0x0000E047u;  // encodes "set y, 7"
    step_instr();
    CHECK(sm.y == 7u);
    CHECK(sm.pc == 1u);
}

TEST_CASE_FIXTURE(SmFix, "MOV PC jumps directly to the moved value") {
    prog[0] = 0xA0A1;  // mov pc, x
    sm.x = 9;
    step_instr();
    CHECK(sm.pc == 9u);
}

TEST_CASE_FIXTURE(SmFix, "the injected instruction's own delay applies, not the OUT EXEC's") {
    prog[0] = 0x65F0;  // out exec, 16  [5]  (this delay must be ignored)
    sm.osr = 0x0000E341u;  // encodes "set y, 1  [3]"
    sm.osr_shift_count = 0;

    auto o = sm.tick();
    CHECK(o.executed);
    CHECK(sm.y == 1u);
    CHECK(sm.pc == 1u);
    for (int i = 0; i < 3; ++i) {
        o = sm.tick();
        CHECK(o.delayed);
    }
    o = sm.tick();
    CHECK(o.executed);  // delay of exactly 3 (the injected instruction's), not 5
}

TEST_CASE_FIXTURE(SmFix, "a disabled state machine does nothing") {
    sm.set_enabled(false);
    prog[0] = 0xE021;  // set x, 1
    const auto o = sm.tick();
    CHECK_FALSE(o.executed);
    CHECK(sm.x == 0);
    CHECK(sm.pc == 0);
}
