// i2c.h - RP2040 I2C (Synopsys DesignWare APB I2C, datasheet 4.3).
//
// Bit-accurate master-mode model: an IC_DATA_CMD write queues a command; it
// executes against the registered slave callback after one byte period (8
// data bits + 1 ACK bit) at the configured SCL rate (IC_SS/FS_SCL_HCNT/LCNT
// against ic_clk == clk_sys), paced by on_cycles(). No multi-master
// arbitration, so there's nothing to hold SCL low except the simulated
// slave: stretch_next() lets the test bench add extra ic_clk cycles to the
// next transaction, standing in for a slave clock-stretching the bus.
//
// Not modelled: 10-bit addressing, slave mode, arbitration loss, DMA.
#ifndef RP2040_PERIPHERALS_I2C_H
#define RP2040_PERIPHERALS_I2C_H

#include <cstdint>
#include <deque>
#include <functional>

#include "core/bus.h"
#include "core/cpu.h"
#include "core/interrupt_controller.h"
#include "core/memory.h"

namespace rp2040 {

class I2c : public BusPeripheral {
public:
    static constexpr std::uint32_t kI2c0Base = 0x40044000u;
    static constexpr std::uint32_t kI2c1Base = 0x40048000u;
    static constexpr std::uint32_t kSize = 0x1000u;
    static constexpr unsigned kI2c0Irq = kExcExternal0 + 23;  // I2C0_IRQ == IRQ23
    static constexpr unsigned kI2c1Irq = kExcExternal0 + 24;
    static constexpr unsigned kFifoDepth = 16;

    I2c(Cpu& cpu, std::uint32_t base, unsigned irq,
        std::uint32_t ic_clk_hz = 125'000'000u, std::uint32_t sys_clk_hz = 125'000'000u)
        : nvic_(cpu), base_(base), irq_(irq), ic_hz_(ic_clk_hz), sys_hz_(sys_clk_hz) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(base_, kSize, this); }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;
    void reset() override;

    // Register a 7-bit slave. On a write transaction `byte` is the data and
    // the callback returns whether it ACKed; on a read it fills `byte` and
    // returns true if data is valid.
    using SlaveFn = std::function<bool(bool is_read, std::uint8_t& byte)>;
    void set_slave(std::uint8_t addr7, SlaveFn fn) { slave_addr_ = addr7; slave_ = std::move(fn); }

    // Optional: called once per I2C STOP condition addressed to the
    // registered slave (a Cmd with .stop set - firmware requests this by
    // setting IC_DATA_CMD.STOP on the final byte of a transaction, matching
    // real DW_apb_i2c). Doesn't change SlaveFn's per-byte shape (existing
    // slaves/tests are unaffected) - it's for a *stateful* slave whose
    // per-byte meaning depends on transaction framing (e.g. an SSD1306's
    // control-byte-then-data-run protocol, where a fresh control byte is
    // only expected at the start of the next transaction).
    void on_stop(std::function<void()> fn) { on_stop_ = std::move(fn); }

    // Hold SCL low for `ic_clk_cycles` extra cycles on the next transaction
    // only (simulated clock stretching by the registered slave).
    void stretch_next(std::uint32_t ic_clk_cycles) { stretch_cycles_ = ic_clk_cycles; }

    // Advance the bus timing by `sys_cycles` clk_sys cycles.
    void on_cycles(std::uint64_t sys_cycles);
    // ic_clk (== clk_sys on the RP2040) and clk_sys in Hz.
    void set_clock_hz(std::uint32_t ic_hz, std::uint32_t sys_hz) {
        ic_hz_ = ic_hz == 0 ? 1u : ic_hz;
        sys_hz_ = sys_hz == 0 ? 1u : sys_hz;
    }

    // Wire the second Cortex-M0+ core into this peripheral's IRQ.
    void connect_core1(Cpu* c) { nvic_.connect(c); }

    // DREQ readiness (datasheet 2.5.3.1: DREQ_I2Cn_TX/RX), gated by
    // IC_DMA_CR.TDMAE/RDMAE like real hardware.
    bool tx_dreq_ready() const;
    bool rx_dreq_ready() const;

private:
    struct Cmd { std::uint8_t byte; bool is_read; bool stop; };

    void refresh_irq();
    std::uint32_t scl_period_cycles() const;  // ic_clk cycles per SCL period
    void run_command();                       // execute the oldest queued Cmd

    InterruptController nvic_;
    std::uint32_t base_;
    unsigned irq_;

    std::uint32_t con_ = 0x65;   // reset-ish: master, 7-bit, fast mode
    std::uint32_t tar_ = 0;
    bool enabled_ = false;
    std::uint32_t dma_cr_ = 0;   // IC_DMA_CR: bit0 RDMAE, bit1 TDMAE

    std::deque<Cmd> tx_cmds_;
    std::deque<std::uint8_t> rx_;
    std::uint32_t raw_intr_ = 0;      // RX_FULL[2], TX_EMPTY[4], TX_ABRT[6], STOP_DET[9]
    std::uint32_t intr_mask_ = 0;
    std::uint32_t tx_abrt_source_ = 0;

    std::uint32_t ss_hcnt_ = 0, ss_lcnt_ = 0;  // IC_SS_SCL_HCNT/LCNT (standard mode)
    std::uint32_t fs_hcnt_ = 0, fs_lcnt_ = 0;  // IC_FS_SCL_HCNT/LCNT (fast mode)

    std::uint32_t ic_hz_;
    std::uint32_t sys_hz_;
    std::uint64_t clk_accum_ = 0;         // sys cycles -> ic_clk cycles
    std::uint32_t byte_cycles_left_ = 0;  // ic_clk cycles left in the transaction in flight
    std::uint32_t stretch_cycles_ = 0;    // extra ic_clk cycles for the next transaction only

    std::uint8_t slave_addr_ = 0xFF;
    SlaveFn slave_;
    std::function<void()> on_stop_;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_I2C_H
