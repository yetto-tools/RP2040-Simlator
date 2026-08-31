// uart.h - RP2040 UART (ARM PrimeCell PL011, datasheet 4.2).
//
// Bit-accurate model: TX and RX are paced by the configured baud rate
// (UARTIBRD/UARTFBRD against UARTCLK == clk_peri, driven by on_cycles()) -
// a byte takes exactly bits_per_frame() bit periods to clock in or out, as
// on real hardware. If IBRD/FBRD are left at their reset value of 0 the baud
// generator is disabled and no data moves at all, matching the datasheet.
//
// There is no physical wire, so framing/parity/break errors can't be derived
// from bit sampling: the test bench tags them explicitly via feed()'s `err`
// parameter. Overrun (OE) is detected for real, when a paced arrival finds
// the RX FIFO already full.
//
// Simplification: FE/PE/BE in UARTRIS are latched sticky at arrival (cleared
// only by a UARTRSR/ECR write), rather than tracking the exact error status
// of whichever byte currently sits at the head of the FIFO as real PL011
// silicon does.
#ifndef RP2040_PERIPHERALS_UART_H
#define RP2040_PERIPHERALS_UART_H

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "core/bus.h"
#include "core/cpu.h"
#include "core/interrupt_controller.h"
#include "core/memory.h"

namespace rp2040 {

class Uart : public BusPeripheral {
public:
    static constexpr std::uint32_t kUart0Base = 0x40034000u;
    static constexpr std::uint32_t kUart1Base = 0x40038000u;
    static constexpr std::uint32_t kSize = 0x1000u;
    static constexpr unsigned kUart0Irq = kExcExternal0 + 20;  // UART0_IRQ == IRQ20
    static constexpr unsigned kUart1Irq = kExcExternal0 + 21;
    static constexpr unsigned kFifoDepth = 32;

    // RX error tags (datasheet 4.2.7 UARTDR / UARTRSR bit layout: FE=bit0,
    // PE=bit1, BE=bit2 of the low nibble).
    enum RxError : std::uint8_t {
        kRxErrNone    = 0,
        kRxErrFraming = 1u << 0,
        kRxErrParity  = 1u << 1,
        kRxErrBreak   = 1u << 2,
    };

    Uart(Cpu& cpu, std::uint32_t base, unsigned irq,
         std::uint32_t uart_clk_hz = 125'000'000u, std::uint32_t sys_clk_hz = 125'000'000u)
        : nvic_(cpu), base_(base), irq_(irq), uart_hz_(uart_clk_hz), sys_hz_(sys_clk_hz) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(base_, kSize, this); }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;

    // --- test-bench / CLI hooks ---------------------------------------
    // Queue a byte from an external device onto the wire; it lands in the RX
    // FIFO after one frame period at the configured baud rate, or is dropped
    // with OE set if the FIFO is already full when it arrives. `err` tags a
    // simulated line error on this byte (there is no wire to derive one from).
    void feed(std::uint8_t byte, std::uint8_t err = kRxErrNone);
    void feed(const std::string& s);
    const std::vector<std::uint8_t>& output() const { return tx_log_; }
    std::string take_output();                    // drains the TX log as a string
    void on_transmit(std::function<void(std::uint8_t)> cb) { tx_cb_ = std::move(cb); }

    // Advance the baud-rate generator by `sys_cycles` clk_sys cycles.
    void on_cycles(std::uint64_t sys_cycles);
    // clk_peri (UARTCLK) and clk_sys in Hz (pushed by the clock tree).
    void set_clock_hz(std::uint32_t uart_hz, std::uint32_t sys_hz) {
        uart_hz_ = uart_hz == 0 ? 1u : uart_hz;
        sys_hz_ = sys_hz == 0 ? 1u : sys_hz;
    }

    // Wire the second Cortex-M0+ core into this peripheral's IRQ.
    void connect_core1(Cpu* c) { nvic_.connect(c); }

private:
    struct RxEntry { std::uint8_t data; std::uint8_t err; };

    void refresh_irq();
    std::uint32_t read_fr() const;
    std::uint32_t read_ris() const;
    std::uint32_t bits_per_frame() const;
    std::uint32_t bit_period_x64() const;  // UARTCLK cycles per bit, fixed-point x64
    void tick_bit();                       // one bit period has elapsed
    void tick_tx_bit();
    void tick_rx_bit();

    InterruptController nvic_;
    std::uint32_t base_;
    unsigned irq_;

    std::deque<RxEntry> rx_;             // the CPU-visible RX FIFO
    std::deque<RxEntry> rx_wire_;        // fed bytes, not yet arrived
    std::deque<std::uint8_t> tx_fifo_;   // the CPU-visible TX FIFO
    std::vector<std::uint8_t> tx_log_;   // bytes that finished transmitting
    std::function<void(std::uint8_t)> tx_cb_;

    std::uint32_t lcr_h_ = 0;
    std::uint32_t cr_ = 0x300;   // reset: TXE|RXE set, UARTEN clear
    std::uint32_t imsc_ = 0;
    std::uint32_t ris_ = 0;      // sticky bits: RT, FE, PE, BE, OE
    std::uint8_t last_rx_err_ = 0;  // FE/PE/BE of the byte last popped via UARTDR
    bool oe_ = false;               // sticky overrun (UARTRSR.OE)

    std::uint32_t ibrd_ = 0;
    std::uint32_t fbrd_ = 0;
    std::uint32_t uart_hz_;
    std::uint32_t sys_hz_;
    std::uint64_t clk_accum_ = 0;      // sys cycles -> UARTCLK cycles
    std::uint32_t bit_accum_x64_ = 0;  // UARTCLK cycles (x64) -> bit periods

    unsigned tx_bits_left_ = 0;   // bit periods left to finish the byte in flight
    std::uint8_t tx_shift_ = 0;
    unsigned rx_bits_left_ = 0;
    RxEntry rx_shift_{};
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_UART_H
