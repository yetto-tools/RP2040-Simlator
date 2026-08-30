#include "peripherals/i2c.h"

namespace rp2040 {

namespace {
enum : std::uint32_t {
    kIC_CON = 0x00, kIC_TAR = 0x04, kIC_SAR = 0x08, kIC_DATA_CMD = 0x10,
    kIC_INTR_STAT = 0x2C, kIC_INTR_MASK = 0x30, kIC_RAW_INTR_STAT = 0x34,
    kIC_CLR_INTR = 0x40, kIC_CLR_RX_UNDER = 0x44, kIC_CLR_RX_OVER = 0x48,
    kIC_CLR_TX_ABRT = 0x54, kIC_CLR_STOP_DET = 0x60,
    kIC_ENABLE = 0x6C, kIC_STATUS = 0x70, kIC_TXFLR = 0x74, kIC_RXFLR = 0x78,
    kIC_TX_ABRT_SOURCE = 0x80,
};
constexpr std::uint32_t kINTR_RX_FULL  = 1u << 2;
constexpr std::uint32_t kINTR_TX_EMPTY = 1u << 4;
constexpr std::uint32_t kINTR_TX_ABRT  = 1u << 6;
constexpr std::uint32_t kINTR_STOP_DET = 1u << 9;

constexpr std::uint32_t kABRT_7B_ADDR_NOACK = 1u << 0;
constexpr std::uint32_t kABRT_TXDATA_NOACK  = 1u << 3;

constexpr std::uint32_t kST_TFNF = 1u << 1;   // TX FIFO not full
constexpr std::uint32_t kST_TFE  = 1u << 2;   // TX FIFO empty
constexpr std::uint32_t kST_RFNE = 1u << 3;   // RX FIFO not empty
constexpr std::uint32_t kST_RFF  = 1u << 4;   // RX FIFO full
}  // namespace

void I2c::refresh_irq() {
    if ((raw_intr_ & intr_mask_) != 0) cpu_.pend_exception(irq_);
    else                               cpu_.clear_pending(irq_);
}

BusResult<std::uint32_t> I2c::bus_read(std::uint32_t offset, BusWidth) {
    switch (offset) {
        case kIC_CON: return {con_, BusStatus::Ok};
        case kIC_TAR: return {tar_, BusStatus::Ok};
        case kIC_ENABLE: return {enabled_ ? 1u : 0u, BusStatus::Ok};
        case kIC_DATA_CMD: {
            std::uint32_t v = 0;
            if (!rx_.empty()) {
                v = rx_.front();
                rx_.pop_front();
                if (rx_.empty()) raw_intr_ &= ~kINTR_RX_FULL;
                refresh_irq();
            }
            return {v, BusStatus::Ok};
        }
        case kIC_STATUS: {
            std::uint32_t st = kST_TFNF | kST_TFE;  // instant TX
            if (!rx_.empty()) st |= kST_RFNE;
            if (rx_.size() >= kFifoDepth) st |= kST_RFF;
            return {st, BusStatus::Ok};
        }
        case kIC_TXFLR: return {0u, BusStatus::Ok};
        case kIC_RXFLR: return {static_cast<std::uint32_t>(rx_.size()), BusStatus::Ok};
        case kIC_RAW_INTR_STAT: return {raw_intr_, BusStatus::Ok};
        case kIC_INTR_STAT: return {raw_intr_ & intr_mask_, BusStatus::Ok};
        case kIC_INTR_MASK: return {intr_mask_, BusStatus::Ok};
        case kIC_TX_ABRT_SOURCE: return {tx_abrt_source_, BusStatus::Ok};
        case kIC_CLR_TX_ABRT:
            raw_intr_ &= ~kINTR_TX_ABRT;
            tx_abrt_source_ = 0;
            refresh_irq();
            return {0u, BusStatus::Ok};
        case kIC_CLR_STOP_DET:
            raw_intr_ &= ~kINTR_STOP_DET;
            refresh_irq();
            return {0u, BusStatus::Ok};
        case kIC_CLR_INTR:
            raw_intr_ = 0;
            tx_abrt_source_ = 0;
            refresh_irq();
            return {0u, BusStatus::Ok};
        case kIC_CLR_RX_UNDER: case kIC_CLR_RX_OVER:
            return {0u, BusStatus::Ok};
        default: return {0u, BusStatus::Ok};
    }
}

BusStatus I2c::bus_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    switch (offset) {
        case kIC_CON: con_ = value; break;
        case kIC_TAR: tar_ = value & 0x3FFu; break;
        case kIC_SAR: break;
        case kIC_ENABLE: enabled_ = (value & 1u) != 0; break;
        case kIC_INTR_MASK: intr_mask_ = value & 0x3FFFu; refresh_irq(); break;
        case kIC_DATA_CMD: {
            if (!enabled_) break;
            const bool is_read = (value & (1u << 8)) != 0;
            const std::uint8_t target = static_cast<std::uint8_t>(tar_ & 0x7Fu);

            if (!slave_ || slave_addr_ != target) {
                tx_abrt_source_ |= kABRT_7B_ADDR_NOACK;
                raw_intr_ |= kINTR_TX_ABRT;
                refresh_irq();
                break;
            }
            if (is_read) {
                std::uint8_t byte = 0;
                if (slave_(true, byte) && rx_.size() < kFifoDepth) {
                    rx_.push_back(byte);
                    raw_intr_ |= kINTR_RX_FULL;
                }
            } else {
                std::uint8_t byte = static_cast<std::uint8_t>(value & 0xFFu);
                if (!slave_(false, byte)) {
                    tx_abrt_source_ |= kABRT_TXDATA_NOACK;
                    raw_intr_ |= kINTR_TX_ABRT;
                }
            }
            if ((value & (1u << 9)) != 0) raw_intr_ |= kINTR_STOP_DET;  // STOP bit
            refresh_irq();
            break;
        }
        default: break;
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
