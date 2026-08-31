// Unit tests for StubPeripheral and the APB blocks the Simulator stubs out.
#include "doctest.h"

#include <cstdint>

#include "core/memory.h"
#include "peripherals/stub_peripheral.h"
#include "simulator.h"

using namespace rp2040;

TEST_CASE("StubPeripheral stores writes, serves defaults, honours atomic aliases") {
    Memory mem;
    StubPeripheral stub{"TEST", 0x40004000u, {{0x10u, 0xCAFEu}}};
    REQUIRE(stub.attach(mem));

    CHECK(mem.read_word(0x40004000u + 0x10u).value == 0xCAFEu);   // default
    CHECK(mem.read_word(0x40004000u + 0x00u).value == 0u);        // unset -> 0

    REQUIRE(mem.write_word(0x40004000u + 0x00u, 0x12345678u) == BusStatus::Ok);
    CHECK(mem.read_word(0x40004000u + 0x00u).value == 0x12345678u);

    // AtomicPeripheral SET alias (+0x2000).
    REQUIRE(mem.write_word(0x40004000u + 0x2000u, 0xF0000000u) == BusStatus::Ok);
    CHECK(mem.read_word(0x40004000u + 0x00u).value == 0xF2345678u);
}

TEST_CASE("the Simulator decodes the stubbed APB blocks without faulting") {
    Simulator sim;
    Memory& mem = sim.memory();

    for (std::uint32_t base : {0x40004000u /*SYSCFG*/, 0x40010000u /*PSM*/,
                              0x40018000u /*IO_QSPI*/, 0x40020000u /*PADS_QSPI*/,
                              0x40030000u /*BUSCTRL*/, 0x40064000u /*VREG*/,
                              0x4006C000u /*TBMAN*/}) {
        CHECK(mem.read_word(base).status == BusStatus::Ok);
        CHECK(mem.write_word(base + 4u, 0x1u) == BusStatus::Ok);
    }
    CHECK(mem.read_word(0x40010000u + 0x0Cu).value == 0x0001FFFFu);  // PSM.DONE
    CHECK(mem.read_word(0x4006C000u).value == 0x1u);                 // TBMAN.PLATFORM = ASIC
}
