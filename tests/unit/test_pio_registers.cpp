// Unit tests for the PIO CPU-facing register block (datasheet 3.7).
#include "doctest.h"

#include <cstdint>

#include "core/memory.h"
#include "peripherals/gpio.h"
#include "pio/pio_block.h"
#include "pio/pio_registers.h"

using namespace rp2040;

namespace {

struct PioRegFix {
    Gpio gpio;
    PioBlock block{gpio, 0};
    Memory mem;
    PioRegisters regs{block, PioRegisters::kPio0Base};

    PioRegFix() { REQUIRE(regs.attach(mem)); }

    std::uint32_t rd(std::uint32_t off) { return mem.read_word(PioRegisters::kPio0Base + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(PioRegisters::kPio0Base + off, v) == BusStatus::Ok);
    }
};

}  // namespace

TEST_CASE("PIO0 register block is routed at 0x50200000") {
    PioRegFix f;
    CHECK(f.mem.read_word(PioRegisters::kPio0Base).status == BusStatus::Ok);
}

TEST_CASE_FIXTURE(PioRegFix, "INSTR_MEM write-through and read-back") {
    wr(0x048 + 4 * 3, 0xE027);          // INSTR_MEM3 = set x, 7
    CHECK(block.instruction(3) == 0xE027);
    CHECK(rd(0x048 + 4 * 3) == 0xE027);
}

TEST_CASE_FIXTURE(PioRegFix, "CTRL enables and restarts state machines") {
    block.sm(1).pc = 9;
    wr(0x000, (1u << 0) | (1u << 1) | (1u << (4 + 1)));  // enable SM0,SM1 + restart SM1
    CHECK(block.sm(0).enabled());
    CHECK(block.sm(1).enabled());
    CHECK_FALSE(block.sm(2).enabled());
    CHECK(block.sm(1).pc == 0);          // restarted to wrap_bottom
    CHECK((rd(0x000) & 0x3u) == 0x3u);
}

TEST_CASE_FIXTURE(PioRegFix, "SM0_SHIFTCTRL / PINCTRL decode into the SM config") {
    // AUTOPULL (17), OUT_SHIFTDIR right (19), PULL_THRESH=8 (bits 29:25)
    wr(0x0C8 + 0x08, (1u << 17) | (1u << 19) | (8u << 25));
    CHECK(block.sm(0).cfg.autopull);
    CHECK(block.sm(0).cfg.out_shiftdir_right);
    CHECK(block.sm(0).cfg.pull_threshold == 8);

    // OUT_BASE=2, OUT_COUNT=4, SET_BASE=10, SET_COUNT=1
    wr(0x0C8 + 0x14, (2u << 0) | (10u << 5) | (4u << 20) | (1u << 26));
    CHECK(block.sm(0).cfg.out_base == 2);
    CHECK(block.sm(0).cfg.out_count == 4);
    CHECK(block.sm(0).cfg.set_base == 10);
    CHECK(block.sm(0).cfg.set_count == 1);
}

TEST_CASE_FIXTURE(PioRegFix, "SM0_EXECCTRL sets wrap and jmp pin") {
    // WRAP_BOTTOM=3 (11:7), WRAP_TOP=7 (16:12), JMP_PIN=12 (28:24)
    wr(0x0C8 + 0x04, (3u << 7) | (7u << 12) | (12u << 24));
    CHECK(block.sm(0).cfg.wrap_bottom == 3);
    CHECK(block.sm(0).cfg.wrap_top == 7);
    CHECK(block.sm(0).cfg.jmp_pin == 12);
}

TEST_CASE_FIXTURE(PioRegFix, "SMx_INSTR injects an instruction without moving PC") {
    block.sm(0).pc = 5;
    wr(0x0C8 + 0x10, 0xE041);            // set y, 1
    CHECK(block.sm(0).y == 1);
    CHECK(block.sm(0).pc == 5);          // unchanged

    wr(0x0C8 + 0x10, 0x0002);            // jmp 2  -> this one *does* move PC
    CHECK(block.sm(0).pc == 2);
}

TEST_CASE_FIXTURE(PioRegFix, "TXF / RXF windows move data through the FIFOs") {
    wr(0x010, 0xAB);                     // TXF0 push
    wr(0x010, 0xCD);                     // TXF0 push
    CHECK((rd(0x00C) & 0xFu) == 2u);     // FLEVEL: TX0 level == 2
    CHECK((rd(0x004) & (1u << 24)) == 0);  // FSTAT: TX0 not empty

    // Program: pull ; mov isr,osr ; push ; jmp 0   -> copies each TX word to RX.
    wr(0x048 + 0, 0x80A0);   // pull block
    wr(0x048 + 4, 0xA0C7);   // mov isr, osr  (dest 110, src 111)
    wr(0x048 + 8, 0x8020);   // push block
    wr(0x048 + 12, 0x0000);  // jmp 0
    wr(0x0C8 + 0x04, (0u << 7) | (3u << 12));   // EXECCTRL wrap 0..3
    wr(0x000, 1u);           // enable SM0

    for (int i = 0; i < 30; ++i) block.tick();
    CHECK((rd(0x004) & (1u << 8)) == 0);   // FSTAT: RX0 not empty
    CHECK(rd(0x020) == 0xAB);              // RXF0 pop (FIFO order)
    CHECK(rd(0x020) == 0xCD);
}

TEST_CASE_FIXTURE(PioRegFix, "IRQ register: read, write-1-clear, and IRQ_FORCE") {
    wr(0x034, (1u << 2) | (1u << 5));    // IRQ_FORCE sets flags 2 and 5
    CHECK(rd(0x030) == ((1u << 2) | (1u << 5)));
    wr(0x030, (1u << 2));                // IRQ write-1-clear flag 2
    CHECK(rd(0x030) == (1u << 5));
}

TEST_CASE("PIO IRQ flag routed to the NVIC through IRQ0_INTE") {
    Gpio gpio;
    PioBlock block(gpio, 0);
    Memory mem;
    RegisterFile regs;
    Cpu cpu(regs, mem);
    PioRegisters pr(block, PioRegisters::kPio0Base);
    REQUIRE(pr.attach(mem));
    pr.connect_nvic(&cpu, PioRegisters::kPio0Irq0);

    // Program: irq 0 ; jmp . ; enable SM0.
    mem.write_word(PioRegisters::kPio0Base + 0x048 + 0, 0xC000);  // irq 0
    mem.write_word(PioRegisters::kPio0Base + 0x048 + 4, 0x0001);  // jmp 1
    mem.write_word(PioRegisters::kPio0Base + 0x0C8 + 0x04, (1u << 12));  // wrap 0..1
    mem.write_word(PioRegisters::kPio0Base + 0x12C, 1u << 8);     // IRQ0_INTE: SM IRQ0 bit

    CHECK_FALSE(cpu.is_pending(PioRegisters::kPio0Irq0));
    mem.write_word(PioRegisters::kPio0Base + 0x000, 1u);          // enable SM0
    block.tick();                                                // SM raises irq 0
    pr.poll_interrupts();
    CHECK((block.irq() & 1u) != 0);
    CHECK(cpu.is_pending(PioRegisters::kPio0Irq0));

    mem.write_word(PioRegisters::kPio0Base + 0x030, 1u);          // PIO IRQ w1c
    pr.poll_interrupts();
    CHECK_FALSE(cpu.is_pending(PioRegisters::kPio0Irq0));
}

TEST_CASE_FIXTURE(PioRegFix, "a blink program configured entirely through registers") {
    gpio.set_funcsel(25, Gpio::kFuncPio0);
    gpio.driver_set_pindir(Gpio::kPio0, 25, true);

    wr(0x048 + 0, 0xE001);   // set pins, 1
    wr(0x048 + 4, 0xE000);   // set pins, 0
    wr(0x0C8 + 0x14, (25u << 5) | (1u << 26));   // PINCTRL: SET_BASE=25, SET_COUNT=1
    wr(0x0C8 + 0x04, (0u << 7) | (1u << 12));    // EXECCTRL: WRAP_BOTTOM=0, WRAP_TOP=1
    wr(0x000, 1u);           // CTRL: enable SM0

    block.tick(); CHECK(gpio.level(25));
    block.tick(); CHECK_FALSE(gpio.level(25));
    block.tick(); CHECK(gpio.level(25));
}
