// Unit tests for the performance profiler (hot-spot histogram, CPI,
// per-vector exception stats).
#include "doctest.h"

#include <array>
#include <cstdint>

#include "debuggers/profiler.h"
#include "exceptions.h"
#include "simulator.h"

using namespace rp2040;

namespace {

void poke(Simulator& sim, std::uint32_t addr, std::initializer_list<std::uint16_t> words) {
    std::vector<std::uint16_t> v(words);
    REQUIRE(sim.memory().load(addr, v.data(), v.size() * 2));
}

}  // namespace

TEST_CASE("profiler builds a hot-spot histogram and CPI") {
    Simulator sim;

    // Vector table: SP, reset -> 0x20000101 (Thumb).
    std::array<std::uint32_t, 16> vt{};
    vt[0] = 0x20002000u;
    vt[1] = 0x20000101u;
    REQUIRE(sim.memory().load(0x20000000u, vt.data(), sizeof(vt)));

    // movs r0,#20 ; L: subs r0,#1 ; bne L ; b .
    poke(sim, 0x20000100u, {0x2014, 0x3801, 0xD1FD, 0xE7FE});
    sim.cpu().set_vtor(0x20000000u);
    sim.cpu().reset();

    Profiler prof(sim);
    const Simulator::RunResult r = prof.run(1000);
    CHECK(r.self_branch);

    const Profiler::Report rep = prof.report();
    CHECK(rep.instructions > 40u);
    CHECK(rep.cycles >= rep.instructions);        // >= 1 cycle each
    CHECK(rep.cycles_per_instruction > 1.0);      // the taken branch costs 3

    REQUIRE_FALSE(rep.hot_spots.empty());
    // The loop body (subs/bne at 0x20000102/104) dominates the histogram.
    CHECK(rep.hot_spots[0].pc >= 0x20000102u);
    CHECK(rep.hot_spots[0].pc <= 0x20000104u);
    CHECK(rep.hot_spots[0].count >= 19u);
}

TEST_CASE("profiler records per-vector exception entries and handler cost") {
    Simulator sim;

    std::array<std::uint32_t, 16> vt{};
    vt[0] = 0x20002000u;
    vt[1] = 0x20000101u;
    vt[kExcSysTick] = 0x20000201u;   // SysTick handler
    REQUIRE(sim.memory().load(0x20000000u, vt.data(), sizeof(vt)));

    poke(sim, 0x20000100u, {0xE7FE});                 // b .   (thread spins)
    poke(sim, 0x20000200u, {0x4770});                 // bx lr (return from handler)
    sim.cpu().set_vtor(0x20000000u);
    sim.cpu().reset();

    Profiler prof(sim);
    prof.run(20);                                     // let the thread spin a bit

    sim.cpu().set_exception_priority(kExcSysTick, 0);
    sim.cpu().pend_exception(kExcSysTick);
    prof.run(20);                                     // entry + handler + return

    const Profiler::Report rep = prof.report();
    REQUIRE_FALSE(rep.exceptions.empty());
    CHECK(rep.exceptions[0].vector == kExcSysTick);
    CHECK(rep.exceptions[0].entries == 1u);
    CHECK(rep.exceptions[0].total_handler_cycles > 0u);
    CHECK(rep.exceptions[0].max_handler_cycles >= rep.exceptions[0].total_handler_cycles);
    CHECK(rep.total_exception_entries == 1u);
}
