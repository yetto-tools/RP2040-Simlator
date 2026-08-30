// Smoke test: proves the build, the core library and the test harness are
// wired together. Real component tests replace/join this as Phase 1 lands.
#include "doctest.h"

#include "rp2040.h"
#include "simulator.h"

TEST_CASE("memory map constants match the datasheet") {
    CHECK(rp2040::kRomBase == 0x00000000u);
    CHECK(rp2040::kFlashBase == 0x10000000u);
    CHECK(rp2040::kSramBase == 0x20000000u);
    CHECK(rp2040::kSramSize == 264u * 1024u);
    CHECK(rp2040::kPio0Base == 0x50200000u);
}

TEST_CASE("simulator starts at cycle zero and advances deterministically") {
    rp2040::Simulator sim;
    CHECK(sim.cycle_count() == 0);

    sim.step();
    CHECK(sim.cycle_count() == 1);

    sim.step(124);
    CHECK(sim.cycle_count() == 125);
}

TEST_CASE("status line reports the core version") {
    rp2040::Simulator sim;
    CHECK(sim.status_line().find(rp2040::version_string()) != std::string::npos);
}
