// spi.h - RP2040 SPI (ARM PrimeCell PL022, datasheet 4.4).
//
// Functional model: each SSPDR write performs a full-duplex byte transfer -
// the TX byte goes to the output log (and an optional transfer callback that
// returns the byte shifted in on MISO), and that response lands in the RX
// FIFO. Bit-rate timing is not modelled.
#ifndef RP2040_PERIPHERALS_SPI_H
#define RP2040_PERIPHERALS_SPI_H

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "core/bus.h"
#include "core/cpu.h"
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

    Spi(Cpu& cpu, std::uint32_t base, unsigned irq) : cpu_(cpu), base_(base), irq_(irq) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(base_, kSize, this); }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;

    // Test bench: bytes an attached slave returns on MISO (consumed one per DR
    // write, once the callback is not set). take_output() drains the MOSI log.
    void feed(std::uint8_t miso_byte) { miso_.push_back(miso_byte); }
    const std::vector<std::uint8_t>& output() const { return mosi_log_; }
    std::vector<std::uint8_t> take_output();
    // Given the MOSI byte, return the MISO byte for the same transfer.
    void on_transfer(std::function<std::uint8_t(std::uint8_t)> cb) { xfer_cb_ = std::move(cb); }

private:
    void refresh_irq();
    std::uint32_t read_sr() const;
    std::uint32_t live_ris() const;

    Cpu& cpu_;
    std::uint32_t base_;
    unsigned irq_;

    std::deque<std::uint8_t> rx_;
    std::deque<std::uint8_t> miso_;
    std::vector<std::uint8_t> mosi_log_;
    std::function<std::uint8_t(std::uint8_t)> xfer_cb_;

    std::uint32_t cr0_ = 0;
    std::uint32_t cr1_ = 0;   // bit1 SSE enable, bit0 LBM loopback
    std::uint32_t imsc_ = 0;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_SPI_H
