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
constexpr std::uint32_t kSR_BSY = 1u << 4;   // busy transmitting/receiving

constexpr std::uint32_t kCR1_LBM = 1u << 0;
constexpr std::uint32_t kCR1_SSE = 1u << 1;

constexpr std::uint32_t kCR0_CPHA = 1u << 6;
constexpr std::uint32_t kCR0_CPOL = 1u << 7;

constexpr std::uint32_t kINT_RX = 1u << 2;   // RXIM / RXRIS (>= half full)
constexpr std::uint32_t kINT_TX = 1u << 3;   // TXIM / TXRIS (<= half empty)

// GPIO role a pin plays when its FUNCSEL selects SPI (datasheet 1.4.3's F1
// column repeats RX/CSn/SCK/TX every 4 pins) - matches Gpio::driver_index().
enum : unsigned { kRoleRx = 0, kRoleCs = 1, kRoleSck = 2, kRoleTx = 3 };

std::uint32_t mask_n(unsigned n) { return n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u); }
}  // namespace

std::vector<std::uint8_t> Spi::take_output() {
    std::vector<std::uint8_t> out;
    out.swap(mosi_log_);
    return out;
}

std::uint32_t Spi::frame_bits() const { return (cr0_ & 0xFu) + 1u; }  // DSS: N -> N+1 bits

std::uint32_t Spi::bit_period_cycles() const {
    // datasheet 4.4.3: SCK freq = SSPCLK / (CPSDVSR x (1 + SCR)); CPSDVSR
    // must be even and >= 2 (0/odd values leave the bit-rate generator off).
    if (cpsdvsr_ < 2 || (cpsdvsr_ & 1u) != 0) return 0;
    const std::uint32_t scr = (cr0_ >> 8) & 0xFFu;
    return cpsdvsr_ * (1u + scr);
}

void Spi::tick_bit() {
    if (bits_left_ > 0) {
        if (--bits_left_ == 0) {
            const std::uint32_t mask = mask_n(frame_bits());
            const std::uint8_t mosi8 = static_cast<std::uint8_t>(tx_shift_ & 0xFFu);
            mosi_log_.push_back(mosi8);

            std::uint16_t miso;
            if ((cr1_ & kCR1_LBM) != 0) {
                miso = tx_shift_;                  // internal loopback
            } else if (xfer_cb_) {
                miso = xfer_cb_(mosi8);
            } else if (!miso_.empty()) {
                miso = miso_.front();
                miso_.pop_front();
            } else {
                miso = 0xFFFFu;                    // idle MISO
            }
            if (rx_.size() < kFifoDepth) rx_.push_back(static_cast<std::uint16_t>(miso & mask));
            refresh_irq();
        }
        return;
    }
    if ((cr1_ & kCR1_SSE) == 0 || tx_fifo_.empty()) return;
    tx_shift_ = tx_fifo_.front();
    tx_fifo_.pop_front();
    bits_left_ = frame_bits();
    refresh_irq();  // TNF may have just become true
}

// One of the two SCK/MOSI edge points within a bit period (datasheet 4.4.3
// waveform diagrams): `at_half_period` fires at the period's midpoint,
// the other at the bit boundary (called right after tick_bit(), so
// bits_left_ already reflects whichever bit is now in flight, if any).
// CPHA selects which half is SCK's active window: CPOL=0/CPHA=0 mode 0
// samples on the boundary->midpoint edge, so the active half is the
// *second* one; CPHA=1 flips that. CPOL just remaps "active" to a level.
void Spi::drive_waveform(bool at_half_period) {
    if (gpio_ == nullptr) return;
    const bool cpol = (cr0_ & kCR0_CPOL) != 0;
    const bool cpha = (cr0_ & kCR0_CPHA) != 0;
    const bool sck_active = bits_left_ > 0 && (cpha ? !at_half_period : at_half_period);
    drive_pins(kRoleSck, sck_active != cpol);

    if (!at_half_period) {
        // MOSI is set up once per bit (valid for the whole period either
        // way - only its position relative to SCK's active window differs
        // between CPHA modes, not when it changes).
        const bool mosi_bit = bits_left_ > 0 && ((tx_shift_ >> (bits_left_ - 1u)) & 1u) != 0;
        drive_pins(kRoleTx, mosi_bit);
    }
}

void Spi::drive_pins(unsigned role, bool level) const {
    const Gpio::Driver drv = (base_ == kSpi0Base) ? Gpio::kSpi0 : Gpio::kSpi1;
    const unsigned instance = (base_ == kSpi0Base) ? 0u : 1u;
    for (unsigned pin = 0; pin < static_cast<unsigned>(Gpio::kNumPins); ++pin) {
        if (gpio_->funcsel(pin) != Gpio::kFuncSpi) continue;
        if ((pin / 8u) % 2u != instance) continue;
        if (pin % 4u != role) continue;
        gpio_->driver_set_pindir(drv, pin, true);
        gpio_->driver_set_pin(drv, pin, level);
    }
}

void Spi::on_cycles(std::uint64_t sys_cycles) {
    if ((cr1_ & kCR1_SSE) == 0) return;
    clk_accum_ += sys_cycles * spi_hz_;
    while (clk_accum_ >= sys_hz_) {
        clk_accum_ -= sys_hz_;
        const std::uint32_t period = bit_period_cycles();
        if (period == 0) continue;  // CPSDVSR not configured: bit-rate generator off
        ++bit_cycle_accum_;
        const std::uint32_t half = period / 2u;
        if (half != 0u && bit_cycle_accum_ == half) drive_waveform(/*at_half_period=*/true);
        if (bit_cycle_accum_ >= period) {
            bit_cycle_accum_ = 0;
            tick_bit();
            drive_waveform(/*at_half_period=*/false);
        }
    }
}

bool Spi::tx_dreq_ready() const {
    return (dmacr_ & (1u << 1)) != 0 && tx_fifo_.size() < kFifoDepth;
}
bool Spi::rx_dreq_ready() const {
    return (dmacr_ & 1u) != 0 && !rx_.empty();
}

std::uint32_t Spi::read_sr() const {
    std::uint32_t sr = 0;
    if (tx_fifo_.empty()) sr |= kSR_TFE;
    if (tx_fifo_.size() < kFifoDepth) sr |= kSR_TNF;
    if (!rx_.empty()) sr |= kSR_RNE;
    if (rx_.size() >= kFifoDepth) sr |= kSR_RFF;
    if (!tx_fifo_.empty() || bits_left_ > 0) sr |= kSR_BSY;
    return sr;
}

std::uint32_t Spi::live_ris() const {
    if ((cr1_ & kCR1_SSE) == 0) return 0;
    std::uint32_t ris = 0;
    if (tx_fifo_.size() < kFifoDepth) ris |= kINT_TX;  // approximation: no half-full watermark
    if (!rx_.empty()) ris |= kINT_RX;
    return ris;
}

void Spi::refresh_irq() {
    if ((live_ris() & imsc_) != 0) nvic_.pend_exception(irq_);
    else                           nvic_.clear_pending(irq_);
}

void Spi::reset() {
    rx_.clear();
    tx_fifo_.clear();
    cr0_ = 0;
    cr1_ = 0;
    cpsdvsr_ = 0;
    imsc_ = 0;
    dmacr_ = 0;
    clk_accum_ = 0;
    bit_cycle_accum_ = 0;
    bits_left_ = 0;
    tx_shift_ = 0;
    refresh_irq();
    // miso_ (queued external-device response bytes), mosi_log_ (a test-bench
    // inspection log) and xfer_cb_ (wiring) are left alone.
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
        case kSSPCPSR: return {cpsdvsr_, BusStatus::Ok};
        case kSSPIMSC: return {imsc_, BusStatus::Ok};
        case kSSPDMACR: return {dmacr_, BusStatus::Ok};
        case kSSPRIS: return {live_ris(), BusStatus::Ok};
        case kSSPMIS: return {live_ris() & imsc_, BusStatus::Ok};
        default: return {0u, BusStatus::Ok};
    }
}

BusStatus Spi::bus_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    switch (offset) {
        case kSSPDR:
            if (tx_fifo_.size() < kFifoDepth) {
                tx_fifo_.push_back(static_cast<std::uint16_t>(value & mask_n(frame_bits())));
            }
            refresh_irq();
            break;
        case kSSPCR0: cr0_ = value; break;
        case kSSPCR1: cr1_ = value; refresh_irq(); break;
        case kSSPCPSR: cpsdvsr_ = value & 0xFFu; break;
        case kSSPIMSC: imsc_ = value & 0xFu; refresh_irq(); break;
        case kSSPICR:  break;  // RORIC/RTIC: no sticky bits modelled
        case kSSPDMACR: dmacr_ = value & 0x3u; break;
        default: break;
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
