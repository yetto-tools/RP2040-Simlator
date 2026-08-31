// spi.h - RP2040 SPI (ARM PrimeCell PL022, datasheet 4.4).
//
// Bit-accurate model: each SSPDR write queues a frame into the TX FIFO; it
// clocks out (and its MISO response clocks in) over frame_bits() bit
// periods, paced by SSPCPSR/SSPCR0.SCR against SSPCLK == clk_peri, driven by
// on_cycles(). Frame size (SSPCR0.DSS, 4-16 bits) is honoured for the
// CPU-facing SSPDR/FIFO width.
//
// Not modelled: CPOL/CPHA have no observable effect (this is a whole-frame
// behavioral model, not a bit-level clock/data waveform simulation, so
// polarity/phase don't change anything there is to test); chip-select lines
// (software bit-bangs CS via GPIO - out of this peripheral's scope).
// The test-bench hooks (feed/on_transfer/output) work in terms of the low 8
// bits of each frame; wider frames are zero-extended/truncated there.
#ifndef RP2040_PERIPHERALS_SPI_H
#define RP2040_PERIPHERALS_SPI_H

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "core/bus.h"
#include "core/cpu.h"
#include "core/interrupt_controller.h"
#include "core/memory.h"

namespace rp2040 {

class Spi : public BusPeripheral {
public:
    static constexpr std::uint32_t kSpi0Base = 0x4003C000u;
    static constexpr std::uint32_t kSpi1Base = 0x40040000u;
    static constexpr std::uint32_t kSize = 0x1000u;
    static constexpr unsigned kSpi0Irq = kExcExternal0 + 18;  // SPI0_IRQ == IRQ18
    static constexpr unsigned kSpi1Irq = kExcExternal0 + 19;
    static constexpr unsigned kFifoDepth = 8;   // PL022 is 8-deep on the RP2040

    Spi(Cpu& cpu, std::uint32_t base, unsigned irq,
        std::uint32_t spi_clk_hz = 125'000'000u, std::uint32_t sys_clk_hz = 125'000'000u)
        : nvic_(cpu), base_(base), irq_(irq), spi_hz_(spi_clk_hz), sys_hz_(sys_clk_hz) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(base_, kSize, this); }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;

    // Test bench: bytes an attached slave returns on MISO (consumed one per
    // completed frame, once the callback is not set; zero-extended to the
    // configured frame width). take_output() drains the MOSI log (low byte
    // of each completed frame, in completion order).
    void feed(std::uint8_t miso_byte) { miso_.push_back(miso_byte); }
    const std::vector<std::uint8_t>& output() const { return mosi_log_; }
    std::vector<std::uint8_t> take_output();
    // Given the MOSI byte, return the MISO byte for the same transfer.
    void on_transfer(std::function<std::uint8_t(std::uint8_t)> cb) { xfer_cb_ = std::move(cb); }

    // Advance the bit-rate generator by `sys_cycles` clk_sys cycles.
    void on_cycles(std::uint64_t sys_cycles);
    // clk_peri (SSPCLK) and clk_sys in Hz (pushed by the clock tree).
    void set_clock_hz(std::uint32_t spi_hz, std::uint32_t sys_hz) {
        spi_hz_ = spi_hz == 0 ? 1u : spi_hz;
        sys_hz_ = sys_hz == 0 ? 1u : sys_hz;
    }

    // Wire the second Cortex-M0+ core into this peripheral's IRQ.
    void connect_core1(Cpu* c) { nvic_.connect(c); }

private:
    void refresh_irq();
    std::uint32_t read_sr() const;
    std::uint32_t live_ris() const;
    std::uint32_t frame_bits() const;      // SSPCR0.DSS -> 4..16
    std::uint32_t bit_period_cycles() const;  // SSPCLK cycles per bit
    void tick_bit();

    InterruptController nvic_;
    std::uint32_t base_;
    unsigned irq_;

    std::deque<std::uint16_t> rx_;
    std::deque<std::uint8_t> miso_;
    std::deque<std::uint16_t> tx_fifo_;
    std::vector<std::uint8_t> mosi_log_;
    std::function<std::uint8_t(std::uint8_t)> xfer_cb_;

    std::uint32_t cr0_ = 0;
    std::uint32_t cr1_ = 0;   // bit1 SSE enable, bit0 LBM loopback
    std::uint32_t cpsdvsr_ = 0;
    std::uint32_t imsc_ = 0;

    std::uint32_t spi_hz_;
    std::uint32_t sys_hz_;
    std::uint64_t clk_accum_ = 0;       // sys cycles -> SSPCLK cycles
    std::uint32_t bit_cycle_accum_ = 0; // SSPCLK cycles -> bit periods
    unsigned bits_left_ = 0;            // bit periods left in the frame in flight
    std::uint16_t tx_shift_ = 0;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_SPI_H
