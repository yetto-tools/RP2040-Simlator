#include "peripherals/uart.h"

namespace rp2040 {

namespace {
enum : std::uint32_t {
    kUARTDR = 0x00, kUARTRSR = 0x04, kUARTFR = 0x18,
    kUARTILPR = 0x20, kUARTIBRD = 0x24, kUARTFBRD = 0x28, kUARTLCR_H = 0x2C,
    kUARTCR = 0x30, kUARTIFLS = 0x34, kUARTIMSC = 0x38, kUARTRIS = 0x3C,
    kUARTMIS = 0x40, kUARTICR = 0x44, kUARTDMACR = 0x48,
};
constexpr std::uint32_t kFR_RXFE = 1u << 4;
constexpr std::uint32_t kFR_RXFF = 1u << 6;
constexpr std::uint32_t kFR_TXFE = 1u << 7;

constexpr std::uint32_t kINT_RX = 1u << 4;   // RXIM / RXRIS
constexpr std::uint32_t kINT_TX = 1u << 5;   // TXIM / TXRIS
constexpr std::uint32_t kINT_RT = 1u << 6;   // receive timeout

constexpr std::uint32_t kCR_UARTEN = 1u << 0;
}  // namespace

void Uart::feed(std::uint8_t byte) {
    if (rx_.size() < kRxFifoDepth) rx_.push_back(byte);
    refresh_irq();
}

void Uart::feed(const std::string& s) {
    for (char c : s) feed(static_cast<std::uint8_t>(c));
}

std::string Uart::take_output() {
    std::string s(tx_log_.begin(), tx_log_.end());
    tx_log_.clear();
    return s;
}

void Uart::transmit(std::uint8_t byte) {
    tx_log_.push_back(byte);
    if (tx_cb_) tx_cb_(byte);
}

std::uint32_t Uart::read_fr() const {
    std::uint32_t fr = kFR_TXFE;  // instant TX: the FIFO is always empty
    if (rx_.empty()) fr |= kFR_RXFE;
    if (rx_.size() >= kRxFifoDepth) fr |= kFR_RXFF;
    return fr;
}

std::uint32_t Uart::read_ris() const {
    std::uint32_t ris = ris_ & kINT_RT;  // RT is the only sticky bit
    if ((cr_ & kCR_UARTEN) != 0) {
        if (!rx_.empty()) ris |= kINT_RX;
        ris |= kINT_TX;  // TX is always ready in this model
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
                v = rx_.front();
                rx_.pop_front();
                refresh_irq();
            }
            return {v, BusStatus::Ok};
        }
        case kUARTFR:   return {read_fr(), BusStatus::Ok};
        case kUARTLCR_H: return {lcr_h_, BusStatus::Ok};
        case kUARTCR:   return {cr_, BusStatus::Ok};
        case kUARTIMSC: return {imsc_, BusStatus::Ok};
        case kUARTRIS:  return {read_ris(), BusStatus::Ok};
        case kUARTMIS:  return {read_ris() & imsc_, BusStatus::Ok};
        case kUARTRSR:  return {0u, BusStatus::Ok};
        default:        return {0u, BusStatus::Ok};
    }
}

BusStatus Uart::bus_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    switch (offset) {
        case kUARTDR:
            transmit(static_cast<std::uint8_t>(value & 0xFFu));
            refresh_irq();
            break;
        case kUARTLCR_H: lcr_h_ = value; break;
        case kUARTCR:    cr_ = value; refresh_irq(); break;
        case kUARTIMSC:  imsc_ = value & 0x7FFu; refresh_irq(); break;
        case kUARTICR:
            ris_ &= ~(value & 0x7FFu);  // write-1-clear (only RT is sticky)
            refresh_irq();
            break;
        case kUARTRSR:  // error clear
        case kUARTIBRD: case kUARTFBRD: case kUARTIFLS:
        case kUARTDMACR: case kUARTILPR:
            break;  // stored/ignored - no baud-rate timing modelled
        default:
            break;
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
