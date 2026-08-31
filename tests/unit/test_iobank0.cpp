// Unit tests for IO_BANK0 GPIO interrupts (datasheet 2.19.6.1).
#include "doctest.h"

#include <cstdint>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "peripherals/gpio.h"
#include "peripherals/iobank0.h"

using namespace rp2040;

namespace {
struct Fix {
    Gpio gpio;
    Memory mem;
    IoBank0 io{gpio};
    RegisterFile r0, r1;
    Cpu c0{r0, mem}, c1{r1, mem};

    Fix() {
        REQUIRE(io.attach(mem));
        io.connect_cores(&c0, &c1);
        for (unsigned p = 0; p < 8; ++p) gpio.set_funcsel(p, Gpio::kFuncSio);
    }
    std::uint32_t rd(std::uint32_t off) { return mem.read_word(IoBank0::kBase + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(IoBank0::kBase + off, v) == BusStatus::Ok);
    }
};
}  // namespace

TEST_CASE_FIXTURE(Fix, "level bits track the live pin; edge bits latch until cleared") {
    io.poll();  // prime the edge detector (all pins low)
    gpio.set_external(3, true);
    io.poll();
    // pin 3 -> group 0, nibble 3: LEVEL_HIGH = bit 13, EDGE_HIGH = bit 15.
    CHECK((rd(0xF0) & (1u << 13)) != 0);   // LEVEL_HIGH
    CHECK((rd(0xF0) & (1u << 15)) != 0);   // EDGE_HIGH latched (rising this poll)

    gpio.set_external(3, false);
    io.poll();
    CHECK((rd(0xF0) & (1u << 13)) == 0);   // LEVEL_HIGH cleared (live)
    CHECK((rd(0xF0) & (1u << 12)) != 0);   // LEVEL_LOW now
    CHECK((rd(0xF0) & (1u << 15)) != 0);   // rising edge still latched
    CHECK((rd(0xF0) & (1u << 14)) != 0);   // EDGE_LOW latched (falling this poll)

    wr(0xF0, (1u << 15) | (1u << 14));     // write-1-clear the edge bits
    CHECK((rd(0xF0) & ((1u << 15) | (1u << 14))) == 0);
    CHECK((rd(0xF0) & (1u << 12)) != 0);   // level bit untouched by the w1c
}

TEST_CASE_FIXTURE(Fix, "an enabled edge pends IO_IRQ_BANK0 on the right core only") {
    io.poll();  // prime, no edges

    wr(0x100, 1u << 15);   // PROC0_INTE0: pin 3 EDGE_HIGH
    gpio.set_external(3, true);
    io.poll();

    CHECK(c0.is_pending(IoBank0::kIrqBank0));
    CHECK_FALSE(c1.is_pending(IoBank0::kIrqBank0));
    CHECK((rd(0x120) & (1u << 15)) != 0);   // PROC0_INTS0 reflects it

    wr(0xF0, 1u << 15);                     // clear the latched edge
    io.poll();
    CHECK_FALSE(c0.is_pending(IoBank0::kIrqBank0));
}

TEST_CASE_FIXTURE(Fix, "GPIOx_CTRL.IRQOVER inverts the edge-detect input") {
    // IRQOVER = invert on pin 3 (bits [31:30] of GPIO3_CTRL at offset 3*8 + 4).
    wr(3u * 8u + 4u, Gpio::kFuncSio | (0x1u << 30));
    io.poll();                                        // prime (pin low -> IRQ sees high)

    gpio.set_external(3, true);                       // pin goes high...
    io.poll();
    // ...but with IRQOVER=invert the IRQ block sees a *falling* edge.
    CHECK((rd(0xF0) & (1u << 14)) != 0);              // EDGE_LOW latched
    CHECK((rd(0xF0) & (1u << 15)) == 0);              // not EDGE_HIGH
    CHECK((rd(0xF0) & (1u << 12)) != 0);              // LEVEL_LOW (inverted)
}

TEST_CASE_FIXTURE(Fix, "PROC1_INTF force raises the interrupt on core 1") {
    io.poll();
    wr(0x130, 1u << 1);    // PROC1_INTE0: pin 0 LEVEL_HIGH
    wr(0x140, 1u << 1);    // PROC1_INTF0: force the same bit
    io.poll();
    CHECK(c1.is_pending(IoBank0::kIrqBank0));
    CHECK_FALSE(c0.is_pending(IoBank0::kIrqBank0));
}
