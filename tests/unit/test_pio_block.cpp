// Unit tests for PIO pin I/O, side-set, WAIT, IRQ, and the PioBlock
// (shared program, 8-bit IRQ register, per-SM fractional clock divider).
// Reference: RP2040 datasheet 3.2-3.5.
#include "doctest.h"

#include <cstdint>

#include "peripherals/gpio.h"
#include "pio/pio_block.h"
#include "pio/state_machine.h"

using namespace rp2040;

namespace {

// Assemble a couple of PIO ops we reuse.
constexpr std::uint16_t pio_set(unsigned data) { return 0xE000u | (0u << 5) | (data & 0x1Fu); }
constexpr std::uint16_t pio_jmp(unsigned addr) { return 0x0000u | (addr & 0x1Fu); }

}  // namespace

TEST_CASE("SET PINS drives the configured pin group via the PIO driver") {
    Gpio gpio;
    PioBlock pio(gpio, 0);
    for (unsigned p = 0; p < 4; ++p) gpio.set_funcsel(p, Gpio::kFuncPio0);

    StateMachine& sm = pio.sm(0);
    sm.cfg.set_base = 0;
    sm.cfg.set_count = 2;
    pio.write_instruction(0, pio_set(0b01));  // set pins, 1  (pin0 high, pin1 low)
    pio.write_instruction(1, pio_jmp(1));
    // pins must be outputs
    gpio.driver_set_pindir(Gpio::kPio0, 0, true);
    gpio.driver_set_pindir(Gpio::kPio0, 1, true);
    sm.set_enabled(true);
    sm.restart();

    pio.tick();
    CHECK(gpio.level(0));
    CHECK_FALSE(gpio.level(1));
}

TEST_CASE("side-set drives pins in parallel with the instruction") {
    Gpio gpio;
    PioBlock pio(gpio, 0);
    gpio.set_funcsel(10, Gpio::kFuncPio0);
    gpio.driver_set_pindir(Gpio::kPio0, 10, true);

    StateMachine& sm = pio.sm(0);
    sm.cfg.sideset_count = 1;         // 1 side-set bit, no optional enable
    sm.cfg.sideset_base = 10;
    // jmp 1, side 1  -> delay/side-set field top bit = side-set data = 1
    pio.write_instruction(0, static_cast<std::uint16_t>(pio_jmp(1) | (1u << 12)));
    pio.write_instruction(1, static_cast<std::uint16_t>(pio_jmp(0) | (0u << 12)));  // side 0
    sm.set_enabled(true);
    sm.restart();

    pio.tick();
    CHECK(gpio.level(10));            // side-set 1
    pio.tick();
    CHECK_FALSE(gpio.level(10));      // side-set 0
}

TEST_CASE("IN PINS samples the configured input group") {
    Gpio gpio;
    PioBlock pio(gpio, 0);
    for (unsigned p = 16; p < 20; ++p) gpio.set_funcsel(p, Gpio::kFuncPio0);
    gpio.set_external(16, true);
    gpio.set_external(17, false);
    gpio.set_external(18, true);
    gpio.set_external(19, false);

    StateMachine& sm = pio.sm(0);
    sm.cfg.in_base = 16;
    sm.cfg.in_shiftdir_right = false;   // shift left: first pin ends up LSB
    pio.write_instruction(0, 0x4004u);  // in pins, 4
    pio.write_instruction(1, pio_jmp(1));
    sm.set_enabled(true);
    sm.restart();

    pio.tick();
    CHECK((sm.isr & 0xF) == 0b0101);    // pins 18,16 high
    CHECK(sm.isr_shift_count == 4);
}

TEST_CASE("WAIT for a GPIO level stalls until the pin matches") {
    Gpio gpio;
    PioBlock pio(gpio, 0);
    gpio.set_funcsel(5, Gpio::kFuncPio0);
    gpio.set_external(5, false);

    StateMachine& sm = pio.sm(0);
    // wait 1 gpio 5 : 001 00000 1 00 00101
    pio.write_instruction(0, static_cast<std::uint16_t>(0x2000u | (1u << 7) | (0u << 5) | 5u));
    pio.write_instruction(1, pio_set(1));
    sm.cfg.set_base = 6;
    sm.cfg.set_count = 1;
    gpio.set_funcsel(6, Gpio::kFuncPio0);
    gpio.driver_set_pindir(Gpio::kPio0, 6, true);
    sm.set_enabled(true);
    sm.restart();

    pio.tick();  pio.tick();  pio.tick();
    CHECK(sm.pc == 0);                  // still waiting
    gpio.set_external(5, true);
    pio.tick();
    CHECK(sm.pc == 1);                  // wait satisfied, advanced
}

TEST_CASE("IRQ set/wait handshake between two state machines") {
    Gpio gpio;
    PioBlock pio(gpio, 0);

    // SM0: irq set 4 ; jmp .        (raise flag 4, then spin)
    pio.write_instruction(0, static_cast<std::uint16_t>(0xC000u | 4u));  // irq 4
    pio.write_instruction(1, pio_jmp(1));
    // SM1: wait 1 irq 4 ; jmp .     (block until flag 4, which it then clears)
    pio.write_instruction(2, static_cast<std::uint16_t>(0x2000u | (1u << 7) | (2u << 5) | 4u));
    pio.write_instruction(3, pio_jmp(3));

    pio.sm(0).cfg.wrap_bottom = 0; pio.sm(0).cfg.wrap_top = 1;
    pio.sm(1).cfg.wrap_bottom = 2; pio.sm(1).cfg.wrap_top = 3;
    pio.sm(0).set_enabled(true); pio.sm(0).restart();
    pio.sm(1).set_enabled(true); pio.sm(1).restart();

    // SMs tick round-robin within a system cycle (SM0 then SM1), so SM0
    // raises flag 4 and the waiting SM1 consumes + clears it the same tick.
    pio.tick();
    CHECK(pio.sm(1).pc == 3);               // WAIT satisfied, SM1 advanced
    CHECK((pio.irq() & (1u << 4)) == 0);    // cleared by the waiting SM

    // With SM0 disabled the flag stays clear and SM1 blocks again.
    pio.set_irq(4, false);
    pio.sm(0).set_enabled(false);
    pio.sm(1).restart();
    pio.tick();
    CHECK(pio.sm(1).pc == 2);               // stalled on WAIT
}

TEST_CASE("per-SM clock divider slows a state machine down") {
    Gpio gpio;
    PioBlock pio(gpio, 0);
    pio.write_instruction(0, pio_jmp(0));   // jmp . (advances an internal counter via pc? use x)
    pio.write_instruction(0, 0xE021u);   // set x, 1
    pio.write_instruction(1, 0xE022u);   // set x, 2
    pio.write_instruction(2, pio_jmp(2));

    StateMachine& sm = pio.sm(0);
    sm.set_enabled(true);
    sm.restart();
    pio.set_clkdiv(0, /*int=*/3, /*frac=*/0);   // one SM step every 3 system clocks

    pio.tick(); CHECK(sm.x == 0);   // acc 256  < 768
    pio.tick(); CHECK(sm.x == 0);   // acc 512
    pio.tick(); CHECK(sm.x == 1);   // acc 768 -> step: set x,1
    pio.tick(); CHECK(sm.x == 1);
    pio.tick(); CHECK(sm.x == 1);
    pio.tick(); CHECK(sm.x == 2);   // second step
}

TEST_CASE("a square-wave program toggles a GPIO every two SM cycles") {
    Gpio gpio;
    PioBlock pio(gpio, 0);
    gpio.set_funcsel(25, Gpio::kFuncPio0);  // the Pico's on-board LED pin
    gpio.driver_set_pindir(Gpio::kPio0, 25, true);

    StateMachine& sm = pio.sm(0);
    sm.cfg.set_base = 25;
    sm.cfg.set_count = 1;
    sm.cfg.wrap_bottom = 0;
    sm.cfg.wrap_top = 1;
    pio.write_instruction(0, pio_set(1));  // set pins, 1
    pio.write_instruction(1, pio_set(0));  // set pins, 0
    sm.set_enabled(true);
    sm.restart();

    pio.tick(); CHECK(gpio.level(25));
    pio.tick(); CHECK_FALSE(gpio.level(25));
    pio.tick(); CHECK(gpio.level(25));
    pio.tick(); CHECK_FALSE(gpio.level(25));
}
