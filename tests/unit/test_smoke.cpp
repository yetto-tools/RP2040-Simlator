// Smoke test: the whole stack wired together - Simulator owns the CPU and
// every peripheral, loads a program, and runs it.
#include "doctest.h"

#include <array>
#include <cstdint>

#include "rp2040.h"
#include "simulator.h"

TEST_CASE("memory map constants match the datasheet") {
    CHECK(rp2040::kRomBase == 0x00000000u);
    CHECK(rp2040::kFlashBase == 0x10000000u);
    CHECK(rp2040::kSramBase == 0x20000000u);
    CHECK(rp2040::kSramSize == 264u * 1024u);
    CHECK(rp2040::kPio0Base == 0x50200000u);
}

TEST_CASE("Simulator runs a program and advances the cycle counter") {
    rp2040::Simulator sim;
    constexpr std::uint32_t base = 0x20000000u;

    // movs r0,#5 ; loop: adds r1,r1,r0 ; subs r0,#1 ; bne loop ; b .
    const std::array<std::uint16_t, 5> prog{0x2005, 0x1809, 0x3801, 0xD1FC, 0xE7FE};
    REQUIRE(sim.memory().load(base, prog.data(), prog.size() * sizeof(std::uint16_t)));
    sim.regs().set_pc(base);

    const auto r = sim.run(500);
    CHECK(r.self_branch);
    CHECK(r.stopped_at == base + 0x08);
    CHECK(sim.regs().get(1) == 15);        // 5+4+3+2+1
    CHECK(sim.cycle_count() > 0);
    CHECK(sim.status_line().find(rp2040::version_string()) != std::string::npos);
}

TEST_CASE("PIO keeps step with the CPU: a blink program toggles GPIO25") {
    rp2040::Simulator sim;
    constexpr std::uint32_t base = 0x20000000u;
    constexpr std::uint32_t pio0 = rp2040::PioRegisters::kPio0Base;

    // CPU program: configure PIO0 SM0 to blink GPIO25, enable it, then spin.
    // We drive it as a sequence of stores; the assembler-free way is to just
    // poke the registers from the harness and let the CPU spin.
    sim.gpio().set_funcsel(25, rp2040::Gpio::kFuncPio0);
    sim.gpio().driver_set_pindir(rp2040::Gpio::kPio0, 25, true);
    REQUIRE(sim.memory().write_word(pio0 + 0x048 + 0, 0xE001) == rp2040::BusStatus::Ok); // set pins,1
    REQUIRE(sim.memory().write_word(pio0 + 0x048 + 4, 0xE000) == rp2040::BusStatus::Ok); // set pins,0
    REQUIRE(sim.memory().write_word(pio0 + 0x0C8 + 0x14, (25u << 5) | (1u << 26)) == rp2040::BusStatus::Ok);
    REQUIRE(sim.memory().write_word(pio0 + 0x0C8 + 0x04, (1u << 12)) == rp2040::BusStatus::Ok);
    REQUIRE(sim.memory().write_word(pio0 + 0x000, 1u) == rp2040::BusStatus::Ok);          // CTRL enable

    const std::array<std::uint16_t, 1> prog{0xE7FE};  // b .
    REQUIRE(sim.memory().load(base, prog.data(), prog.size() * sizeof(std::uint16_t)));
    sim.regs().set_pc(base);

    // Each `b .` costs 3 cycles -> PIO0 ticks 3x per CPU instruction.
    bool seen_high = false;
    bool seen_low = false;
    for (int i = 0; i < 20; ++i) {
        sim.step();
        if (sim.gpio().level(25)) seen_high = true; else seen_low = true;
    }
    CHECK(seen_high);
    CHECK(seen_low);
}
