// Unit tests for the RP2040 TIMER (datasheet 4.6).
#include "doctest.h"

#include <cstdint>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "peripherals/timer.h"

using namespace rp2040;

namespace {

struct TimerFix {
    RegisterFile regs;
    Memory mem;
    Cpu cpu{regs, mem};
    Timer timer{cpu, /*cycles_per_us=*/10};   // 10 sys clocks == 1 us

    TimerFix() { REQUIRE(timer.attach(mem)); }

    std::uint32_t rd(std::uint32_t off) { return mem.read_word(Timer::kBase + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(Timer::kBase + off, v) == BusStatus::Ok);
    }
};

}  // namespace

TEST_CASE_FIXTURE(TimerFix, "the microsecond counter advances from system cycles") {
    timer.on_cycles(35);                       // 3 us + 5 leftover
    CHECK(timer.now_us() == 3);
    timer.on_cycles(5);                        // completes the 4th us
    CHECK(timer.now_us() == 4);
    CHECK(rd(0x28) == 4);                      // TIMERAWL
    CHECK(rd(0x24) == 0);                      // TIMERAWH
}

TEST_CASE_FIXTURE(TimerFix, "TIMELR read latches the high word for TIMEHR") {
    // Force a large counter via the write pair (TIMEHW then TIMELW).
    wr(0x00, 0x00000002u);                     // TIMEHW
    wr(0x04, 0xFFFFFFF0u);                     // TIMELW -> counter = 0x2_FFFFFFF0
    CHECK(rd(0x0C) == 0xFFFFFFF0u);            // TIMELR (also latches high)
    CHECK(rd(0x08) == 0x00000002u);            // TIMEHR from the latch
}

TEST_CASE_FIXTURE(TimerFix, "an alarm fires when the low counter matches, then auto-disarms") {
    wr(0x38, 0x1u);                            // INTE alarm 0
    wr(0x10, 5u);                              // ALARM0 = 5 us  (arms it)
    CHECK((rd(0x20) & 1u) != 0);               // ARMED

    timer.on_cycles(40);                       // 4 us - not yet
    CHECK_FALSE(cpu.is_pending(Timer::kIrq0));
    timer.on_cycles(10);                       // 5 us - fires
    CHECK((rd(0x34) & 1u) != 0);               // INTR alarm 0
    CHECK((rd(0x20) & 1u) == 0);               // auto-disarmed
    CHECK(cpu.is_pending(Timer::kIrq0));       // NVIC IRQ0 pended

    wr(0x34, 1u);                              // INTR write-1-clear
    CHECK_FALSE(cpu.is_pending(Timer::kIrq0)); // IRQ deasserts
}

TEST_CASE_FIXTURE(TimerFix, "INTE gates whether an alarm reaches the NVIC") {
    wr(0x10, 2u);                              // ALARM0 = 2 us, armed, INTE still 0
    timer.on_cycles(30);                       // past 2 us
    CHECK((rd(0x34) & 1u) != 0);               // INTR set
    CHECK_FALSE(cpu.is_pending(Timer::kIrq0)); // but masked
    wr(0x38, 1u);                              // enable -> now asserts
    CHECK(cpu.is_pending(Timer::kIrq0));
}

TEST_CASE_FIXTURE(TimerFix, "INTF forces an alarm interrupt without the timer firing") {
    wr(0x38, 0x4u);                            // INTE alarm 2
    wr(0x3C, 0x4u);                            // INTF alarm 2
    CHECK(cpu.is_pending(Timer::kIrq0 + 2));
    CHECK((rd(0x40) & 0x4u) != 0);             // INTS
    wr(0x3C, 0x0u);
    CHECK_FALSE(cpu.is_pending(Timer::kIrq0 + 2));
}

TEST_CASE_FIXTURE(TimerFix, "the atomic SET alias reaches INTE") {
    wr(0x2000u + 0x38u, 0x4u);          // hw_set_bits(&timer->inte, 1<<2)
    CHECK((rd(0x38) & 0x4u) != 0);
    wr(0x3000u + 0x38u, 0x4u);          // hw_clear_bits
    CHECK((rd(0x38) & 0x4u) == 0);
}

TEST_CASE_FIXTURE(TimerFix, "PAUSE freezes the counter") {
    wr(0x30, 1u);                              // PAUSE
    timer.on_cycles(100);
    CHECK(timer.now_us() == 0);
    wr(0x30, 0u);
    timer.on_cycles(20);
    CHECK(timer.now_us() == 2);
}
