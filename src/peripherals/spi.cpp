#include "peripherals/spi.h"

namespace rp2040 {

namespace {
enum : std::uint32_t {
    kSSPCR0 = 0x00, kSSPCR1 = 0x04, kSSPDR = 0x08, kSSPSR = 0x0C,
    kSSPCPSR = 0x10, kSSPIMSC = 0x14, kSSPRIS = 0x18, kSSPMIS = 0x1C,
    kSSPICR = 0x20, kSSPDMACR = 0x24,
};
constexpr std::uint32_t kSR_TFE = 1u << 0;   // TX FIFO empty
constexpr std::uint32_t kSR_TNF = 1u << 1;   // TX FIFO not full
constexpr std::uint32_t kSR_RNE = 1u << 2;   // RX FIFO not empty
constexpr std::uint32_t kSR_RFF = 1u << 3;   // RX FIFO full

constexpr std::uint32_t kCR1_LBM = 1u << 0;
constexpr std::uint32_t kCR1_SSE = 1u << 1;

constexpr std::uint32_t kINT_RX = 1u << 2;   // RXIM / RXRIS (>= half full)
constexpr std::uint32_t kINT_TX = 1u << 3;   // TXIM / TXRIS (<= half empty)
}  // namespace

std::vector<std::uint8_t> Spi::take_output() {
    std::vector<std::uint8_t> out;
    out.swap(mosi_log_);
    return out;
}

std::uint32_t Spi::read_sr() const {
    std::uint32_t sr = kSR_TFE | kSR_TNF;  // instant TX: always empty / not full
    if (!rx_.empty()) sr |= kSR_RNE;
    if (rx_.size() >= kFifoDepth) sr |= kSR_RFF;
    return sr;
}

std::uint32_t Spi::live_ris() const {
    if ((cr1_ & kCR1_SSE) == 0) return 0;
    std::uint32_t ris = kINT_TX;  // TX always ready in this model
    if (!rx_.empty()) ris |= kINT_RX;
    return ris;
}

void Spi::refresh_irq() {
    if ((live_ris() & imsc_) != 0) nvic_.pend_exception(irq_);
    else                           nvic_.clear_pending(irq_);
}

BusResult<std::uint32_t> Spi::bus_read(std::uint32_t offset, BusWidth) {
    switch (offset) {
        case kSSPDR: {
            std::uint32_t v = 0;
            if (!rx_.empty()) {
                v = rx_.front();
                rx_.pop_front();
                refresh_irq();
            }
            return {v, BusStatus::Ok};
        }
        case kSSPCR0: return {cr0_, BusStatus::Ok};
        case kSSPCR1: return {cr1_, BusStatus::Ok};
        case kSSPSR:  return {read_sr(), BusStatus::Ok};
        case kSSPIMSC: return {imsc_, BusStatus::Ok};
        case kSSPRIS: return {live_ris(), BusStatus::Ok};
        case kSSPMIS: return {live_ris() & imsc_, BusStatus::Ok};
        default: return {0u, BusStatus::Ok};
    }
}

BusStatus Spi::bus_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    switch (offset) {
        case kSSPDR: {
            const std::uint8_t mosi = static_cast<std::uint8_t>(value & 0xFFu);
            mosi_log_.push_back(mosi);

            std::uint8_t miso;
            if ((cr1_ & kCR1_LBM) != 0) {
                miso = mosi;                       // internal loopback
            } else if (xfer_cb_) {
                miso = xfer_cb_(mosi);
            } else if (!miso_.empty()) {
                miso = miso_.front();
                miso_.pop_front();
            } else {
                miso = 0xFF;                       // idle MISO
            }
            if (rx_.size() < kFifoDepth) rx_.push_back(miso);
            refresh_irq();
            break;
        }
        case kSSPCR0: cr0_ = value; break;
        case kSSPCR1: cr1_ = value; refresh_irq(); break;
        case kSSPIMSC: imsc_ = value & 0xFu; refresh_irq(); break;
        case kSSPICR:  break;  // RORIC/RTIC: no sticky bits modelled
        case kSSPCPSR: case kSSPDMACR: break;
        default: break;
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
