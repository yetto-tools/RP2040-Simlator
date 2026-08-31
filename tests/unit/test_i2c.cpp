// Unit tests for the RP2040 I2C (DW_apb_i2c bit-accurate model, datasheet 4.3).
#include "doctest.h"

#include <cstdint>
#include <vector>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "peripherals/i2c.h"

using namespace rp2040;

namespace {

struct I2cFix {
    RegisterFile regs;
    Memory mem;
    Cpu cpu{regs, mem};
    I2c i2c{cpu, I2c::kI2c0Base, I2c::kI2c0Irq};

    I2cFix() {
        REQUIRE(i2c.attach(mem));
        wr(0x1C, 2u);   // IC_FS_SCL_HCNT (reset CON default is fast mode)
        wr(0x20, 2u);   // IC_FS_SCL_LCNT -> SCL period = 4 ic_clk cycles
        wr(0x6C, 1u);   // IC_ENABLE
    }
    std::uint32_t rd(std::uint32_t off) { return mem.read_word(I2c::kI2c0Base + off).value; }
    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(I2c::kI2c0Base + off, v) == BusStatus::Ok);
    }
    // One byte transaction = 9 SCL periods (8 data bits + ACK) * 4 cyc/period
    // = 36 ic_clk cycles; 50 gives comfortable margin.
    void advance_cmds(int n) { i2c.on_cycles(static_cast<std::uint64_t>(n) * 50u); }
};

}  // namespace

TEST_CASE_FIXTURE(I2cFix, "a write transaction reaches the registered slave") {
    std::vector<std::uint8_t> got;
    i2c.set_slave(0x50, [&](bool is_read, std::uint8_t& b) {
        if (is_read) return false;
        got.push_back(b);
        return true;
    });
    wr(0x04, 0x50);                  // IC_TAR
    wr(0x10, 0xAB);                  // IC_DATA_CMD: write 0xAB
    wr(0x10, 0xCD | (1u << 9));      // write 0xCD + STOP
    advance_cmds(2);

    REQUIRE(got.size() == 2);
    CHECK(got[0] == 0xAB);
    CHECK(got[1] == 0xCD);
    CHECK((rd(0x34) & (1u << 6)) == 0);   // no TX_ABRT
    CHECK((rd(0x34) & (1u << 9)) != 0);   // STOP_DET
}

TEST_CASE_FIXTURE(I2cFix, "a command does not complete before its byte period elapses") {
    i2c.set_slave(0x50, [](bool, std::uint8_t&) { return true; });
    wr(0x04, 0x50);
    wr(0x10, 0xAB);
    i2c.on_cycles(30);    // < 36 cycles needed (9 SCL periods * 4 cyc/period)
    CHECK((rd(0x70) & (1u << 2)) == 0);   // IC_STATUS.TFE not yet set - still in flight
    i2c.on_cycles(20);    // now past 50 total
    CHECK((rd(0x70) & (1u << 2)) != 0);   // TFE once the command retires
}

TEST_CASE_FIXTURE(I2cFix, "a read transaction fills the RX FIFO from the slave") {
    std::uint8_t next = 0x10;
    i2c.set_slave(0x3C, [&](bool is_read, std::uint8_t& b) {
        if (!is_read) return true;
        b = next++;
        return true;
    });
    wr(0x04, 0x3C);
    wr(0x10, 1u << 8);              // IC_DATA_CMD: read command
    wr(0x10, 1u << 8);
    advance_cmds(2);

    CHECK(rd(0x78) == 2u);          // IC_RXFLR
    CHECK((rd(0x70) & (1u << 3)) != 0);  // IC_STATUS.RFNE
    CHECK(rd(0x10) == 0x10);        // IC_DATA_CMD read pops
    CHECK(rd(0x10) == 0x11);
    CHECK((rd(0x70) & (1u << 3)) == 0);
}

TEST_CASE_FIXTURE(I2cFix, "addressing an absent slave raises TX_ABRT (address NACK)") {
    wr(0x04, 0x22);
    wr(0x10, 0x00);
    advance_cmds(1);
    CHECK((rd(0x34) & (1u << 6)) != 0);          // RAW_INTR_STAT.TX_ABRT
    CHECK((rd(0x80) & (1u << 0)) != 0);          // IC_TX_ABRT_SOURCE.7B_ADDR_NOACK
    rd(0x54);                                    // IC_CLR_TX_ABRT
    CHECK((rd(0x34) & (1u << 6)) == 0);
    CHECK(rd(0x80) == 0u);
}

TEST_CASE_FIXTURE(I2cFix, "RX_FULL interrupt is gated by IC_INTR_MASK and reaches the NVIC") {
    i2c.set_slave(0x40, [](bool is_read, std::uint8_t& b) { if (is_read) b = 0x99; return true; });
    wr(0x04, 0x40);
    wr(0x30, 1u << 2);             // IC_INTR_MASK: RX_FULL
    CHECK_FALSE(cpu.is_pending(I2c::kI2c0Irq));

    wr(0x10, 1u << 8);            // read -> RX FIFO fills
    advance_cmds(1);
    CHECK(cpu.is_pending(I2c::kI2c0Irq));
    CHECK(rd(0x10) == 0x99);      // drain -> deassert
    CHECK_FALSE(cpu.is_pending(I2c::kI2c0Irq));
}

TEST_CASE_FIXTURE(I2cFix, "transactions are ignored while IC_ENABLE is clear") {
    bool touched = false;
    i2c.set_slave(0x10, [&](bool, std::uint8_t&) { touched = true; return true; });
    wr(0x6C, 0u);                 // disable
    wr(0x04, 0x10);
    wr(0x10, 0x00);
    advance_cmds(1);
    CHECK_FALSE(touched);
}

TEST_CASE_FIXTURE(I2cFix, "stretch_next holds the transaction for extra ic_clk cycles") {
    i2c.set_slave(0x50, [](bool, std::uint8_t&) { return true; });
    i2c.stretch_next(20);          // simulated clock stretching by the slave
    wr(0x04, 0x50);
    wr(0x10, 0x00);
    i2c.on_cycles(50);             // the un-stretched 36-cycle budget has passed
    CHECK((rd(0x70) & (1u << 2)) == 0);   // still stretched, not done
    i2c.on_cycles(10);             // now past 36 + 20 = 56 total
    CHECK((rd(0x70) & (1u << 2)) != 0);   // done
}

TEST_CASE_FIXTURE(I2cFix, "with no bit-rate configured (HCNT=LCNT=0), no transaction completes") {
    wr(0x1C, 0u);
    wr(0x20, 0u);
    bool touched = false;
    i2c.set_slave(0x50, [&](bool, std::uint8_t&) { touched = true; return true; });
    wr(0x04, 0x50);
    wr(0x10, 0x00);
    advance_cmds(5);
    CHECK_FALSE(touched);
    CHECK(rd(0x74) == 1u);         // IC_TXFLR: the command is still queued
}
