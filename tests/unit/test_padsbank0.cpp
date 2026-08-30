// Unit tests for PADS_BANK0 (datasheet 2.19.6.3).
#include "doctest.h"

#include <cstdint>

#include "core/memory.h"
#include "peripherals/gpio.h"
#include "peripherals/padsbank0.h"

using namespace rp2040;

TEST_CASE("PADS_BANK0 routes PUE / PDE to the GPIO model") {
    Gpio gpio;
    Memory mem;
    PadsBank0 pads(gpio);
    REQUIRE(pads.attach(mem));

    const std::uint32_t gpio7 = PadsBank0::kBase + 0x04u + 7u * 4u;
    mem.write_word(gpio7, 1u << 3);        // PUE
    gpio.set_funcsel(7, Gpio::kFuncSio);
    CHECK(gpio.level(7));                  // pulled up

    mem.write_word(gpio7, 1u << 2);        // PDE
    CHECK_FALSE(gpio.level(7));            // pulled down

    CHECK((mem.read_word(gpio7).value & 0xFFu) == (1u << 2));
}

TEST_CASE("PADS_BANK0 supports the atomic SET / CLR aliases") {
    Gpio gpio;
    Memory mem;
    PadsBank0 pads(gpio);
    REQUIRE(pads.attach(mem));

    const std::uint32_t reg = 0x04u + 3u * 4u;  // GPIO3 pad
    mem.write_word(PadsBank0::kBase + 0x2000u + reg, 1u << 1);   // SET SCHMITT
    CHECK((mem.read_word(PadsBank0::kBase + reg).value & (1u << 1)) != 0);
    mem.write_word(PadsBank0::kBase + 0x3000u + reg, 1u << 1);   // CLR
    CHECK((mem.read_word(PadsBank0::kBase + reg).value & (1u << 1)) == 0);
}
