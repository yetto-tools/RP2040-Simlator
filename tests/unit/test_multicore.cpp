// Integration tests for the dual-core model: SIO mailbox FIFO, spinlocks,
// per-core CPUID, and the core-1 launch sequence.
#include "doctest.h"

#include <array>
#include <cstdint>

#include "core/memory.h"
#include "peripherals/gpio.h"
#include "peripherals/sio.h"
#include "peripherals/timer.h"
#include "simulator.h"

using namespace rp2040;

namespace {
constexpr std::uint32_t kSio = Sio::kBase;

// SIO in isolation (no CPU/scheduler), so on_cycles() can be driven directly
// instead of through Simulator::step()'s instruction-timed loop.
struct SioFix {
    Gpio gpio;
    Memory mem;
    Sio sio{gpio};
    SioFix() { REQUIRE(sio.attach(mem)); }
    std::uint32_t rd(std::uint32_t a) { return mem.read_word(a).value; }
    void wr(std::uint32_t a, std::uint32_t v) {
        REQUIRE(mem.write_word(a, v) == BusStatus::Ok);
    }
};
}  // namespace

TEST_CASE("SIO mailbox: core 0 pushes, core 1 pops (and vice versa)") {
    Simulator sim;
    Memory& mem = sim.memory();

    // Active core is 0 during a normal harness access.
    REQUIRE(mem.write_word(kSio + 0x054u, 0xDEADBEEFu) == BusStatus::Ok);   // FIFO_WR
    // The core-1 launch machine echoes the word back onto the 1->0 FIFO too;
    // core 0 can read its own inbox.
    CHECK((mem.read_word(kSio + 0x050u).value & 1u) != 0);                  // FIFO_ST.VLD
    CHECK(mem.read_word(kSio + 0x058u).value == 0xDEADBEEFu);              // FIFO_RD
}

TEST_CASE("SIO spinlocks: first reader takes it, others get 0 until released") {
    Simulator sim;
    Memory& mem = sim.memory();
    const std::uint32_t lock7 = kSio + 0x100u + 7u * 4u;

    CHECK(mem.read_word(lock7).value == (1u << 7));   // acquired
    CHECK(mem.read_word(lock7).value == 0u);          // contended
    REQUIRE(mem.write_word(lock7, 0u) == BusStatus::Ok);  // release
    CHECK(mem.read_word(lock7).value == (1u << 7));   // re-acquired
}

TEST_CASE("launching core 1 through the mailbox starts it executing") {
    Simulator sim;
    Memory& mem = sim.memory();
    constexpr std::uint32_t c0 = 0x20000000u;
    constexpr std::uint32_t c1 = 0x20010000u;
    constexpr std::uint32_t c1_stack = 0x20040000u;

    // core 0: spin. core 1: movs r0,#0x42 ; b .
    const std::array<std::uint16_t, 1> prog0{0xE7FE};
    const std::array<std::uint16_t, 2> prog1{0x2042, 0xE7FE};
    REQUIRE(mem.load(c0, prog0.data(), prog0.size() * 2));
    REQUIRE(mem.load(c1, prog1.data(), prog1.size() * 2));
    sim.regs(0).set_pc(c0);

    CHECK_FALSE(sim.core1_running());

    // Send the pico-sdk launch sequence: 0, 0, 1, vtor, sp, entry.
    for (std::uint32_t w : {0u, 0u, 1u, c1, c1_stack, (c1 | 1u)}) {
        REQUIRE(mem.write_word(kSio + 0x054u, w) == BusStatus::Ok);
    }
    CHECK(sim.core1_running());
    CHECK(sim.regs(1).sp() == c1_stack);
    CHECK(sim.regs(1).pc() == c1);

    for (int i = 0; i < 10; ++i) sim.step();
    CHECK(sim.regs(1).get(0) == 0x42);      // core 1 ran its program
    CHECK(sim.regs(0).pc() == c0);          // core 0 still spinning
}

TEST_CASE("a peripheral IRQ is delivered to core 1 (per-core NVIC)") {
    Simulator sim;
    Memory& mem = sim.memory();
    constexpr std::uint32_t c0 = 0x20000000u;
    constexpr std::uint32_t c1_vt = 0x20010000u;
    constexpr std::uint32_t c1_code = 0x20010100u;
    constexpr std::uint32_t c1_handler = 0x20010200u;

    const std::array<std::uint16_t, 1> prog0{0xE7FE};                 // core 0: b .
    const std::array<std::uint16_t, 1> prog1{0xE7FE};                 // core 1: b .
    const std::array<std::uint16_t, 1> handler{0xE7FE};               // handler: b . (stay put)
    REQUIRE(mem.load(c0, prog0.data(), 2));
    REQUIRE(mem.load(c1_code, prog1.data(), 2));
    REQUIRE(mem.load(c1_handler, handler.data(), 2));
    REQUIRE(mem.write_word(c1_vt + 0u, 0x20040000u) == BusStatus::Ok);
    REQUIRE(mem.write_word(c1_vt + 4u, c1_code | 1u) == BusStatus::Ok);
    REQUIRE(mem.write_word(c1_vt + 4u * Timer::kIrq0, c1_handler | 1u) == BusStatus::Ok);
    sim.regs(0).set_pc(c0);

    for (std::uint32_t w : {0u, 0u, 1u, c1_vt, 0x20040000u, (c1_code | 1u)}) {
        REQUIRE(mem.write_word(kSio + 0x054u, w) == BusStatus::Ok);
    }
    REQUIRE(sim.core1_running());

    // core 1 enables TIMER_IRQ_0; core 0 does not.
    sim.cpu(1).set_irq_enabled(0, true);
    sim.cpu(1).set_exception_priority(Timer::kIrq0, 0);

    // Arm alarm 0 for "now" so it fires on the next microsecond tick.
    REQUIRE(mem.write_word(Timer::kBase + 0x38u, 0x1u) == BusStatus::Ok);   // INTE alarm 0
    REQUIRE(mem.write_word(Timer::kBase + 0x10u, 0u) == BusStatus::Ok);     // ALARM0 = 0

    for (int i = 0; i < 300; ++i) sim.step();

    CHECK(sim.regs(1).pc() >= c1_handler);        // core 1 vectored to the handler
    CHECK(sim.regs(1).pc() < c1_handler + 8u);
    CHECK(sim.cpu(1).current_exception() == Timer::kIrq0);
    CHECK(sim.regs(0).pc() == c0);                // core 0 never took it
    CHECK(sim.cpu(0).current_exception() == 0u);
}

TEST_CASE("per-core CPUID reflects the active core") {
    Simulator sim;
    // In step() core 0 runs with CPUID 0; the SIO switches to 1 for core 1.
    // A harness read sees the last-set active core (0 after a step()).
    sim.step();
    CHECK(sim.memory().read_word(kSio + 0x000u).value == 0u);
}

// SIO integer divider (datasheet 2.3.1.6). picoOS's uart_putu32 (and hence
// every shell command that prints a number, e.g. "suma 7 3") busy-waits on
// CSR.READY with interrupts masked; before this peripheral existed, READY
// never went high and the CPU spun forever.
TEST_CASE_FIXTURE(SioFix, "SIO divider: unsigned divide takes 8 clk_sys cycles") {
    wr(Sio::kBase + 0x060u, 100u);   // UDIVIDEND
    wr(Sio::kBase + 0x064u, 7u);     // UDIVISOR - (re)starts the calculation
    CHECK((rd(Sio::kBase + 0x078u) & 1u) == 0u);   // CSR.READY clear mid-flight
    sio.on_cycles(7);
    CHECK((rd(Sio::kBase + 0x078u) & 1u) == 0u);   // still not ready after 7 cycles
    sio.on_cycles(1);
    CHECK((rd(Sio::kBase + 0x078u) & 1u) != 0u);   // ready on the 8th
    CHECK(rd(Sio::kBase + 0x070u) == 14u);         // QUOTIENT
    CHECK(rd(Sio::kBase + 0x074u) == 2u);          // REMAINDER
}

TEST_CASE_FIXTURE(SioFix, "SIO divider: signed divide truncates toward zero") {
    wr(Sio::kBase + 0x068u, static_cast<std::uint32_t>(-7));  // SDIVIDEND
    wr(Sio::kBase + 0x06Cu, 2u);                                // SDIVISOR
    sio.on_cycles(8);
    CHECK(static_cast<std::int32_t>(rd(Sio::kBase + 0x070u)) == -3);
    CHECK(static_cast<std::int32_t>(rd(Sio::kBase + 0x074u)) == -1);
}

TEST_CASE_FIXTURE(SioFix, "SIO divider: INT32_MIN / -1 wraps instead of trapping") {
    wr(Sio::kBase + 0x068u, 0x80000000u);   // INT32_MIN
    wr(Sio::kBase + 0x06Cu, 0xFFFFFFFFu);   // -1
    sio.on_cycles(8);
    CHECK(rd(Sio::kBase + 0x070u) == 0x80000000u);
    CHECK(rd(Sio::kBase + 0x074u) == 0u);
}

TEST_CASE_FIXTURE(SioFix, "SIO divider: divide by zero sets QUOTIENT=-1, REMAINDER=DIVIDEND") {
    wr(Sio::kBase + 0x060u, 42u);
    wr(Sio::kBase + 0x064u, 0u);
    sio.on_cycles(8);
    CHECK(rd(Sio::kBase + 0x070u) == 0xFFFFFFFFu);
    CHECK(rd(Sio::kBase + 0x074u) == 42u);
}

TEST_CASE_FIXTURE(SioFix, "SIO divider: DIRTY clears on QUOTIENT read, survives REMAINDER read") {
    wr(Sio::kBase + 0x060u, 9u);
    wr(Sio::kBase + 0x064u, 2u);
    sio.on_cycles(8);
    CHECK((rd(Sio::kBase + 0x078u) & 2u) != 0u);   // DIRTY set by the writes
    (void)rd(Sio::kBase + 0x074u);                 // REMAINDER read leaves it set
    CHECK((rd(Sio::kBase + 0x078u) & 2u) != 0u);
    (void)rd(Sio::kBase + 0x070u);                 // QUOTIENT read clears it
    CHECK((rd(Sio::kBase + 0x078u) & 2u) == 0u);
}

TEST_CASE_FIXTURE(SioFix, "SIO divider: a write mid-flight restarts the 8-cycle countdown") {
    wr(Sio::kBase + 0x060u, 100u);
    wr(Sio::kBase + 0x064u, 7u);
    sio.on_cycles(6);
    wr(Sio::kBase + 0x064u, 3u);   // new divisor before the first finished
    sio.on_cycles(6);
    CHECK((rd(Sio::kBase + 0x078u) & 1u) == 0u);   // still not ready: restarted at 6
    sio.on_cycles(2);
    CHECK(rd(Sio::kBase + 0x070u) == 33u);         // 100 / 3
    CHECK(rd(Sio::kBase + 0x074u) == 1u);
}
