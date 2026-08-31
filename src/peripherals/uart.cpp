#include "peripherals/uart.h"

namespace rp2040 {

namespace {
enum : std::uint32_t {
    kUARTDR = 0x00, kUARTRSR = 0x04, kUARTFR = 0x18,
    kUARTILPR = 0x20, kUARTIBRD = 0x24, kUARTFBRD = 0x28, kUARTLCR_H = 0x2C,
    kUARTCR = 0x30, kUARTIFLS = 0x34, kUARTIMSC = 0x38, kUARTRIS = 0x3C,
    kUARTMIS = 0x40, kUARTICR = 0x44, kUARTDMACR = 0x48,
};
constexpr std::uint32_t kFR_BUSY = 1u << 3;
constexpr std::uint32_t kFR_RXFE = 1u << 4;
constexpr std::uint32_t kFR_TXFF = 1u << 5;
constexpr std::uint32_t kFR_RXFF = 1u << 6;
constexpr std::uint32_t kFR_TXFE = 1u << 7;

constexpr std::uint32_t kINT_RX = 1u << 4;   // RXIM / RXRIS
constexpr std::uint32_t kINT_TX = 1u << 5;   // TXIM / TXRIS
constexpr std::uint32_t kINT_RT = 1u << 6;   // receive timeout
constexpr std::uint32_t kINT_FE = 1u << 7;
constexpr std::uint32_t kINT_PE = 1u << 8;
constexpr std::uint32_t kINT_BE = 1u << 9;
constexpr std::uint32_t kINT_OE = 1u << 10;
constexpr std::uint32_t kINT_STICKY = kINT_RT | kINT_FE | kINT_PE | kINT_BE | kINT_OE;

constexpr std::uint32_t kCR_UARTEN = 1u << 0;
constexpr std::uint32_t kCR_TXE = 1u << 8;
constexpr std::uint32_t kCR_RXE = 1u << 9;

constexpr std::uint32_t kLCR_BRK = 1u << 0;
constexpr std::uint32_t kLCR_PEN = 1u << 1;
constexpr std::uint32_t kLCR_STP2 = 1u << 3;
}  // namespace

void Uart::feed(std::uint8_t byte, std::uint8_t err) {
    rx_wire_.push_back({byte, err});
}

void Uart::feed(const std::string& s) {
    for (char c : s) feed(static_cast<std::uint8_t>(c));
}

std::string Uart::take_output() {
    std::string s(tx_log_.begin(), tx_log_.end());
    tx_log_.clear();
    return s;
}

std::uint32_t Uart::bits_per_frame() const {
    const std::uint32_t data_bits = 5u + ((lcr_h_ >> 5) & 0x3u);  // WLEN: 00->5 .. 11->8
    const std::uint32_t parity_bits = (lcr_h_ & kLCR_PEN) ? 1u : 0u;
    const std::uint32_t stop_bits = (lcr_h_ & kLCR_STP2) ? 2u : 1u;
    return 1u /*start*/ + data_bits + parity_bits + stop_bits;
}

std::uint32_t Uart::bit_period_x64() const {
    // BAUDDIV = IBRD + FBRD/64; one bit takes 16 x BAUDDIV UARTCLK cycles
    // (datasheet 4.2.4 / PL011 TRM). In x64 fixed point: 1024*IBRD + 16*FBRD.
    return 1024u * ibrd_ + 16u * fbrd_;
}

void Uart::tick_tx_bit() {
    if (tx_bits_left_ > 0) {
        if (--tx_bits_left_ == 0) {
            tx_log_.push_back(tx_shift_);
            if (tx_cb_) tx_cb_(tx_shift_);
        }
        return;
    }
    if ((cr_ & kCR_TXE) == 0 || (lcr_h_ & kLCR_BRK) != 0 || tx_fifo_.empty()) return;
    tx_shift_ = tx_fifo_.front();
    tx_fifo_.pop_front();
    tx_bits_left_ = bits_per_frame();
}

void Uart::tick_rx_bit() {
    if (rx_bits_left_ > 0) {
        if (--rx_bits_left_ == 0) {
            if (rx_.size() < kFifoDepth) {
                rx_.push_back(rx_shift_);
                if (rx_shift_.err & kRxErrFraming) ris_ |= kINT_FE;
                if (rx_shift_.err & kRxErrParity)  ris_ |= kINT_PE;
                if (rx_shift_.err & kRxErrBreak)   ris_ |= kINT_BE;
            } else {
                oe_ = true;  // FIFO full: the character is lost
                ris_ |= kINT_OE;
            }
        }
        return;
    }
    if ((cr_ & kCR_RXE) == 0 || rx_wire_.empty()) return;
    rx_shift_ = rx_wire_.front();
    rx_wire_.pop_front();
    rx_bits_left_ = bits_per_frame();
}

void Uart::tick_bit() {
    tick_tx_bit();
    tick_rx_bit();
    refresh_irq();
}

void Uart::on_cycles(std::uint64_t sys_cycles) {
    if ((cr_ & kCR_UARTEN) == 0) return;
    clk_accum_ += sys_cycles * uart_hz_;
    while (clk_accum_ >= sys_hz_) {
        clk_accum_ -= sys_hz_;
        const std::uint32_t period = bit_period_x64();
        if (period == 0) continue;  // IBRD=FBRD=0: baud generator disabled
        bit_accum_x64_ += 64u;
        while (bit_accum_x64_ >= period) {
            bit_accum_x64_ -= period;
            tick_bit();
        }
    }
}

std::uint32_t Uart::read_fr() const {
    std::uint32_t fr = 0;
    if (tx_fifo_.empty() && tx_bits_left_ == 0) fr |= kFR_TXFE;
    if (tx_fifo_.size() >= kFifoDepth) fr |= kFR_TXFF;
    if (rx_.empty()) fr |= kFR_RXFE;
    if (rx_.size() >= kFifoDepth) fr |= kFR_RXFF;
    if (!tx_fifo_.empty() || tx_bits_left_ > 0) fr |= kFR_BUSY;
    return fr;
}

bool Uart::tx_dreq_ready() const {
    return (dmacr_ & (1u << 1)) != 0 && tx_fifo_.size() < kFifoDepth;
}
bool Uart::rx_dreq_ready() const {
    return (dmacr_ & 1u) != 0 && !rx_.empty();
}

std::uint32_t Uart::read_ris() const {
    std::uint32_t ris = ris_ & kINT_STICKY;
    if ((cr_ & kCR_UARTEN) != 0) {
        if (!rx_.empty()) ris |= kINT_RX;
        if (tx_fifo_.size() < kFifoDepth) ris |= kINT_TX;  // approximation: no IFLS watermark
    }
    return ris;
}

void Uart::refresh_irq() {
    const std::uint32_t mis = read_ris() & imsc_;
    if (mis != 0) nvic_.pend_exception(irq_);
    else          nvic_.clear_pending(irq_);
}

BusResult<std::uint32_t> Uart::bus_read(std::uint32_t offset, BusWidth) {
    switch (offset) {
        case kUARTDR: {
            std::uint32_t v = 0;
            if (!rx_.empty()) {
                const RxEntry e = rx_.front();
                rx_.pop_front();
                last_rx_err_ = e.err;
                v = e.data | (static_cast<std::uint32_t>(e.err) << 8) | (oe_ ? (1u << 11) : 0u);
                refresh_irq();
            }
            return {v, BusStatus::Ok};
        }
        case kUARTRSR: return {static_cast<std::uint32_t>(last_rx_err_) | (oe_ ? 8u : 0u), BusStatus::Ok};
        case kUARTFR:   return {read_fr(), BusStatus::Ok};
        case kUARTLCR_H: return {lcr_h_, BusStatus::Ok};
        case kUARTIBRD: return {ibrd_, BusStatus::Ok};
        case kUARTFBRD: return {fbrd_, BusStatus::Ok};
        case kUARTCR:   return {cr_, BusStatus::Ok};
        case kUARTIMSC: return {imsc_, BusStatus::Ok};
        case kUARTDMACR: return {dmacr_, BusStatus::Ok};
        case kUARTRIS:  return {read_ris(), BusStatus::Ok};
        case kUARTMIS:  return {read_ris() & imsc_, BusStatus::Ok};
        default:        return {0u, BusStatus::Ok};
    }
}

BusStatus Uart::bus_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    switch (offset) {
        case kUARTDR:
            if (tx_fifo_.size() < kFifoDepth) tx_fifo_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
            refresh_irq();
            break;
        case kUARTRSR:  // ECR: any write clears the latched error flags
            last_rx_err_ = 0;
            oe_ = false;
            ris_ &= ~(kINT_FE | kINT_PE | kINT_BE | kINT_OE);
            refresh_irq();
            break;
        case kUARTLCR_H: lcr_h_ = value; break;
        case kUARTIBRD:  ibrd_ = value & 0xFFFFu; break;
        case kUARTFBRD:  fbrd_ = value & 0x3Fu; break;
        case kUARTCR:    cr_ = value; refresh_irq(); break;
        case kUARTIMSC:  imsc_ = value & 0x7FFu; refresh_irq(); break;
        case kUARTICR:
            ris_ &= ~(value & kINT_STICKY);  // write-1-clear
            refresh_irq();
            break;
        case kUARTDMACR: dmacr_ = value & 0x7u; break;
        case kUARTIFLS: case kUARTILPR:
            break;  // stored/ignored (IFLS watermark not modelled)
        default:
            break;
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
