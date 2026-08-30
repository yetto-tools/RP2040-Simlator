// Unit tests for the SYSINFO identification block (datasheet 2.20).
#include "doctest.h"

#include <cstdint>

#include "core/memory.h"
#include "peripherals/sysinfo.h"

using namespace rp2040;

TEST_CASE("SYSINFO reports plausible RP2040 silicon identity") {
    Memory mem;
    Sysinfo si;
    REQUIRE(si.attach(mem));

    const std::uint32_t chip_id = mem.read_word(Sysinfo::kBase + 0x00).value;
    CHECK((chip_id & 0xFFFu) == 0x927u);              // MANUFACTURER
    CHECK(((chip_id >> 12) & 0xFFFFu) == 0x0002u);    // PART == RP2040
    CHECK(((chip_id >> 28) & 0xFu) == 2u);            // REVISION (B1/B2)

    CHECK(mem.read_word(Sysinfo::kBase + 0x04).value == (1u << 1));  // PLATFORM = ASIC
    CHECK(mem.read_word(Sysinfo::kBase + 0x40).value == Sysinfo::kGitRef);
}

TEST_CASE("SYSINFO registers are read-only") {
    Memory mem;
    Sysinfo si;
    REQUIRE(si.attach(mem));
    CHECK(mem.write_word(Sysinfo::kBase + 0x00, 0xDEADBEEFu) == BusStatus::WriteToReadOnly);
    CHECK(mem.read_word(Sysinfo::kBase + 0x00).value == Sysinfo::kChipId);
}
