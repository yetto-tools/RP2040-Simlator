// spi.h - RP2040 SPI (ARM PrimeCell PL022, datasheet 4.4).
//
// Bit-accurate model: each SSPDR write queues a frame into the TX FIFO; it
// clocks out (and its MISO response clocks in) over frame_bits() bit
// periods, paced by SSPCPSR/SSPCR0.SCR against SSPCLK == clk_peri, driven by
// on_cycles(). Frame size (SSPCR0.DSS, 4-16 bits) is honoured for the
// CPU-facing SSPDR/FIFO width.
//
// CPOL/CPHA (datasheet 4.4.3, "Motorola SPI frame format"): when connected
// to a real Gpio (connect_gpio() - optional, so every existing test-bench
// use of this class without it is unaffected), SCK/MOSI are additionally
// driven bit-by-bit onto whichever GPIO(s) currently select this
// instance's SCK/TX role (see Gpio::driver_index()'s SPI note) - SCK idles
// at CPOL's level and its active half-period lands before (CPHA=1) or
// after (CPHA=0) the bit period's midpoint, matching the datasheet's
// waveform diagrams. This is a second, purely observable layer alongside
// the existing byte-atomic FIFO/xfer_cb_ data path below, which still
// drives every actual register/interrupt/DMA-readiness/callback outcome
// exactly as before - CPOL/CPHA change nothing about the bytes
// transferred, only the electrical waveform an external observer (a test,
// or a bit-banged PIO program) would see.
//
// Chip-select lines: RP2040's own SPI0/1 CSn pins (datasheet 1.4.3's F1
// column) aren't modelled as peripheral-driven output - and shouldn't be,
// since real firmware (pico-sdk's own spi_init() included) never uses
// them either; CS is always software-managed via a plain SIO GPIO on this
// chip, matching the class's own scope here.
//
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
#include "peripherals/gpio.h"

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
    void reset() override;

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

    // Optional: drive the real SCK/MOSI waveform onto whichever GPIO(s)
    // select this instance's SPI function (see the class comment). Every
    // existing use of this class without calling this is unaffected.
    void connect_gpio(Gpio& g) { gpio_ = &g; }

    // DREQ readiness (datasheet 2.5.3.1: DREQ_SPIn_TX/RX), gated by
    // SSPDMACR.TXDMAE/RXDMAE like real hardware.
    bool tx_dreq_ready() const;
    bool rx_dreq_ready() const;

private:
    void refresh_irq();
    std::uint32_t read_sr() const;
    std::uint32_t live_ris() const;
    std::uint32_t frame_bits() const;      // SSPCR0.DSS -> 4..16
    std::uint32_t bit_period_cycles() const;  // SSPCLK cycles per bit
    void tick_bit();
    void drive_waveform(bool at_half_period);  // CPOL/CPHA-timed SCK/MOSI edges
    void drive_pins(unsigned role, bool level) const;

    InterruptController nvic_;
    std::uint32_t base_;
    unsigned irq_;
    Gpio* gpio_ = nullptr;

    std::deque<std::uint16_t> rx_;
    std::deque<std::uint8_t> miso_;
    std::deque<std::uint16_t> tx_fifo_;
    std::vector<std::uint8_t> mosi_log_;
    std::function<std::uint8_t(std::uint8_t)> xfer_cb_;

    std::uint32_t cr0_ = 0;
    std::uint32_t cr1_ = 0;   // bit1 SSE enable, bit0 LBM loopback
    std::uint32_t cpsdvsr_ = 0;
    std::uint32_t imsc_ = 0;
    std::uint32_t dmacr_ = 0;   // bit0 RXDMAE, bit1 TXDMAE

    std::uint32_t spi_hz_;
    std::uint32_t sys_hz_;
    std::uint64_t clk_accum_ = 0;       // sys cycles -> SSPCLK cycles
    std::uint32_t bit_cycle_accum_ = 0; // SSPCLK cycles -> bit periods
    unsigned bits_left_ = 0;            // bit periods left in the frame in flight
    std::uint16_t tx_shift_ = 0;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_SPI_H
