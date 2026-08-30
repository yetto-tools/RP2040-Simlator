// uart.h - RP2040 UART (ARM PrimeCell PL011, datasheet 4.2).
//
// Functional model: transmitted bytes are delivered instantly to an output
// log (and optional callback); received bytes are fed in by the test bench /
// CLI. Baud-rate timing is not modelled - the TX side is always ready.
#ifndef RP2040_PERIPHERALS_UART_H
#define RP2040_PERIPHERALS_UART_H

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "core/bus.h"
#include "core/cpu.h"
#include "core/memory.h"

namespace rp2040 {

class Uart : public BusPeripheral {
public:
    static constexpr std::uint32_t kUart0Base = 0x40034000u;
    static constexpr std::uint32_t kUart1Base = 0x40038000u;
    static constexpr std::uint32_t kSize = 0x1000u;
    static constexpr unsigned kUart0Irq = kExcExternal0 + 20;  // UART0_IRQ == IRQ20
    static constexpr unsigned kUart1Irq = kExcExternal0 + 21;
    static constexpr unsigned kRxFifoDepth = 32;

    Uart(Cpu& cpu, std::uint32_t base, unsigned irq)
        : cpu_(cpu), base_(base), irq_(irq) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(base_, kSize, this); }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;

    // --- test-bench / CLI hooks ---------------------------------------
    void feed(std::uint8_t byte);                 // an external device sends a byte
    void feed(const std::string& s);
    const std::vector<std::uint8_t>& output() const { return tx_log_; }
    std::string take_output();                    // drains the TX log as a string
    void on_transmit(std::function<void(std::uint8_t)> cb) { tx_cb_ = std::move(cb); }

private:
    void transmit(std::uint8_t byte);
    void refresh_irq();
    std::uint32_t read_fr() const;
    std::uint32_t read_ris() const;

    Cpu& cpu_;
    std::uint32_t base_;
    unsigned irq_;

    std::deque<std::uint8_t> rx_;
    std::vector<std::uint8_t> tx_log_;
    std::function<void(std::uint8_t)> tx_cb_;

    std::uint32_t lcr_h_ = 0;
    std::uint32_t cr_ = 0x300;   // reset: TXE|RXE set, UARTEN clear
    std::uint32_t imsc_ = 0;
    std::uint32_t ris_ = 0;      // sticky bits (not the live RX/TX levels)
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_UART_H
