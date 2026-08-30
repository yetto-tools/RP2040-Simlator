// Unit tests for the GPIO model + SIO / IO_BANK0 register windows.
// Reference: RP2040 datasheet 2.3.1 (SIO), 2.19 (GPIO).
#include "doctest.h"

#include <cstdint>

#include "core/memory.h"
#include "peripherals/gpio.h"
#include "peripherals/iobank0.h"
#include "peripherals/sio.h"

using namespace rp2040;

namespace {

struct GpioFix {
    Gpio gpio;
    Memory mem;
    Sio sio{gpio};
    IoBank0 iobank{gpio};

    GpioFix() {
        REQUIRE(sio.attach(mem));
        REQUIRE(iobank.attach(mem));
    }
    std::uint32_t rd(std::uint32_t a) { return mem.read_word(a).value; }
    void wr(std::uint32_t a, std::uint32_t v) {
        REQUIRE(mem.write_word(a, v) == BusStatus::Ok);
    }
    void funcsel(unsigned pin, std::uint8_t f) {
        wr(IoBank0::kBase + 8u * pin + 4u, f);
    }
};

}  // namespace

TEST_CASE("SIO region (0xD0000000) is routed") {
    GpioFix f;
    CHECK(f.mem.read_word(Sio::kBase).status == BusStatus::Ok);   // CPUID
}

TEST_CASE_FIXTURE(GpioFix, "SIO drives a SIO-function pin high") {
    funcsel(3, Gpio::kFuncSio);
    wr(Sio::kBase + 0x020, 1u << 3);   // GPIO_OE  = pin 3
    wr(Sio::kBase + 0x010, 1u << 3);   // GPIO_OUT = pin 3

    CHECK(gpio.pad_driving(3));
    CHECK(gpio.pad_level(3));
    CHECK(gpio.level(3));
    CHECK((rd(Sio::kBase + 0x004) & (1u << 3)) != 0);  // GPIO_IN reads it back
}

TEST_CASE_FIXTURE(GpioFix, "GPIO_OUT_SET / _CLR / _XOR are atomic aliases") {
    funcsel(0, Gpio::kFuncSio);
    funcsel(1, Gpio::kFuncSio);
    wr(Sio::kBase + 0x024, 0x3u);       // GPIO_OE_SET: pins 0,1

    wr(Sio::kBase + 0x014, 0x1u);       // OUT_SET pin 0
    CHECK(rd(Sio::kBase + 0x010) == 0x1u);
    wr(Sio::kBase + 0x014, 0x2u);       // OUT_SET pin 1
    CHECK(rd(Sio::kBase + 0x010) == 0x3u);
    wr(Sio::kBase + 0x018, 0x1u);       // OUT_CLR pin 0
    CHECK(rd(Sio::kBase + 0x010) == 0x2u);
    wr(Sio::kBase + 0x01C, 0x3u);       // OUT_XOR pins 0,1
    CHECK(rd(Sio::kBase + 0x010) == 0x1u);
}

TEST_CASE_FIXTURE(GpioFix, "an input pin reads external stimulus, then the pull") {
    funcsel(5, Gpio::kFuncSio);  // SIO function, but OE clear -> input
    gpio.set_external(5, true);
    CHECK(gpio.level(5));
    CHECK((rd(Sio::kBase + 0x004) & (1u << 5)) != 0);

    gpio.clear_external(5);
    gpio.set_pulls(5, /*up=*/true, /*down=*/false);
    CHECK(gpio.level(5));               // pull-up
    gpio.set_pulls(5, false, true);
    CHECK_FALSE(gpio.level(5));         // pull-down
    gpio.set_pulls(5, false, false);
    CHECK_FALSE(gpio.level(5));         // floating -> 0
}

TEST_CASE_FIXTURE(GpioFix, "FUNCSEL routes the pad to PIO0, not SIO") {
    // SIO asks pin 7 high, but the pad is owned by PIO0.
    wr(Sio::kBase + 0x020, 1u << 7);
    wr(Sio::kBase + 0x010, 1u << 7);
    funcsel(7, Gpio::kFuncPio0);
    CHECK_FALSE(gpio.pad_driving(7));   // PIO0 has not driven it

    gpio.driver_set_pindir(Gpio::kPio0, 7, true);
    gpio.driver_set_pin(Gpio::kPio0, 7, true);
    CHECK(gpio.pad_driving(7));
    CHECK(gpio.pad_level(7));

    // Switch back to SIO: its earlier request takes effect.
    funcsel(7, Gpio::kFuncSio);
    CHECK(gpio.pad_level(7));
}

TEST_CASE_FIXTURE(GpioFix, "GPIOx_CTRL reads back and STATUS shows levels") {
    funcsel(2, Gpio::kFuncSio);
    CHECK((rd(IoBank0::kBase + 8u * 2u + 4u) & 0x1Fu) == Gpio::kFuncSio);

    wr(Sio::kBase + 0x020, 1u << 2);
    wr(Sio::kBase + 0x010, 1u << 2);
    const std::uint32_t status = rd(IoBank0::kBase + 8u * 2u + 0u);
    CHECK((status & (1u << 9)) != 0);   // OUTTOPAD
    CHECK((status & (1u << 17)) != 0);  // INFROMPAD
}
