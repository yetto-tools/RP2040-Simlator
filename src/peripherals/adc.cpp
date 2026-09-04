#include "peripherals/adc.h"

namespace rp2040 {

namespace {
enum : std::uint32_t {
    kCS = 0x00, kRESULT = 0x04, kFCS = 0x08, kFIFO = 0x0C, kDIV = 0x10,
    kINTR = 0x14, kINTE = 0x18, kINTF = 0x1C, kINTS = 0x20,
};
constexpr std::uint32_t kCS_EN         = 1u << 0;
constexpr std::uint32_t kCS_TS_EN      = 1u << 1;
constexpr std::uint32_t kCS_START_ONCE = 1u << 2;
constexpr std::uint32_t kCS_START_MANY = 1u << 3;
constexpr std::uint32_t kCS_READY      = 1u << 8;
constexpr unsigned      kCS_AINSEL_LSB = 12;   // [14:12]
constexpr unsigned      kCS_RROBIN_LSB = 16;   // [23:16]

constexpr std::uint32_t kFCS_EN      = 1u << 0;
constexpr std::uint32_t kFCS_SHIFT   = 1u << 1;
constexpr std::uint32_t kFCS_ERR     = 1u << 2;
constexpr std::uint32_t kFCS_DREQ_EN = 1u << 3;
constexpr std::uint32_t kFCS_EMPTY = 1u << 8;
constexpr std::uint32_t kFCS_FULL  = 1u << 9;
constexpr std::uint32_t kFCS_OVER  = 1u << 10;
constexpr std::uint32_t kFCS_UNDER = 1u << 11;
constexpr unsigned      kFCS_LEVEL_LSB  = 16;  // [19:16]
constexpr unsigned      kFCS_THRESH_LSB = 24;  // [27:24]
}  // namespace

bool Adc::dreq_ready() const {
    return (fcs_ & kFCS_DREQ_EN) != 0 && !fifo_.empty();
}

void Adc::set_input(unsigned channel, std::uint16_t raw12) {
    if (channel < kNumInputs) input_[channel] = raw12 & 0x0FFFu;
}

unsigned Adc::next_rrobin(unsigned from) const {
    const std::uint32_t mask = (cs_ >> kCS_RROBIN_LSB) & 0x1Fu;
    if (mask == 0) return from;
    for (unsigned step = 1; step <= kNumInputs; ++step) {
        const unsigned ch = (from + step) % kNumInputs;
        if ((mask >> ch) & 1u) return ch;
    }
    return from;
}

void Adc::convert() {
    unsigned ch = (cs_ >> kCS_AINSEL_LSB) & 0x7u;
    if (ch >= kNumInputs) ch = 0;

    std::uint16_t code = input_[ch];
    if (ch == kTempChannel && (cs_ & kCS_TS_EN) == 0) code = 0;
    code &= 0x0FFFu;

    result_ = code;
    cs_ |= kCS_READY;

    if ((fcs_ & kFCS_EN) != 0) {
        std::uint16_t sample = code;
        if ((fcs_ & kFCS_SHIFT) != 0) sample = static_cast<std::uint16_t>((code >> 4) & 0xFFu);
        if (fifo_.size() >= kFifoDepth) {
            fcs_ |= kFCS_OVER;  // FIFO overflow: sample dropped
        } else {
            fifo_.push_back(sample);
        }
    }

    // Round-robin advance for the next START_MANY conversion.
    const std::uint32_t rrobin = (cs_ >> kCS_RROBIN_LSB) & 0x1Fu;
    if (rrobin != 0) {
        const unsigned nxt = next_rrobin(ch);
        cs_ = (cs_ & ~(0x7u << kCS_AINSEL_LSB)) | (nxt << kCS_AINSEL_LSB);
    }
    refresh_irq();
}

void Adc::refresh_irq() {
    const unsigned thresh = (fcs_ >> kFCS_THRESH_LSB) & 0xFu;
    const bool fifo_int = fifo_.size() >= (thresh == 0 ? 1u : thresh);
    const std::uint32_t ris = fifo_int ? 1u : 0u;
    const std::uint32_t mis = (ris | (intf_ & 1u)) & (inte_ & 1u);
    if (mis != 0) nvic_.pend_exception(kIrq);
    else          nvic_.clear_pending(kIrq);
}

void Adc::on_cycles(std::uint64_t sys_cycles) {
    if ((cs_ & kCS_EN) == 0) return;

    adc_clk_accum_ += sys_cycles * adc_hz_;
    while (adc_clk_accum_ >= sys_hz_) {
        adc_clk_accum_ -= sys_hz_;
        // one ADC clock
        if (converting_) {
            if (--conv_countdown_ == 0) {
                converting_ = false;
                convert();
            }
        }
        if (!converting_ && (cs_ & kCS_START_MANY) != 0) {
            converting_ = true;
            conv_countdown_ = 96u + ((div_ >> 8) & 0xFFFFu);
            cs_ &= ~kCS_READY;
        }
    }
}

void Adc::reset() {
    fifo_.clear();
    result_ = 0;
    cs_ = 0;
    fcs_ = 0;
    div_ = 0;
    inte_ = 0;
    intf_ = 0;
    adc_clk_accum_ = 0;
    conv_countdown_ = 0;
    converting_ = false;
    refresh_irq();
    // input_ is the test bench's simulated analog stimulus (the "world"
    // outside the ADC, not one of its registers) - left alone, same as a
    // real GPIO pad's voltage doesn't change just because the ADC reset.
}

BusResult<std::uint32_t> Adc::reg_read(std::uint32_t offset, BusWidth) {
    switch (offset) {
        case kCS: return {cs_, BusStatus::Ok};
        case kRESULT: return {result_, BusStatus::Ok};
        case kFCS: {
            std::uint32_t v = fcs_ & ~(0xFu << kFCS_LEVEL_LSB);
            v |= (static_cast<std::uint32_t>(fifo_.size()) & 0xFu) << kFCS_LEVEL_LSB;
            if (fifo_.empty()) v |= kFCS_EMPTY;
            if (fifo_.size() >= kFifoDepth) v |= kFCS_FULL;
            return {v, BusStatus::Ok};
        }
        case kFIFO: {
            std::uint32_t v = 0;
            if (fifo_.empty()) {
                fcs_ |= kFCS_UNDER;
            } else {
                v = fifo_.front();
                fifo_.pop_front();
                refresh_irq();
            }
            return {v, BusStatus::Ok};
        }
        case kDIV:  return {div_, BusStatus::Ok};
        case kINTR: return {fifo_.size() >= 1u ? 1u : 0u, BusStatus::Ok};
        case kINTE: return {inte_, BusStatus::Ok};
        case kINTF: return {intf_, BusStatus::Ok};
        case kINTS: {
            const std::uint32_t ris = (fifo_.size() >= 1u) ? 1u : 0u;
            return {(ris | intf_) & inte_, BusStatus::Ok};
        }
        default: return {0u, BusStatus::Ok};
    }
}

BusStatus Adc::reg_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    switch (offset) {
        case kCS: {
            // READY is read-only; START_ONCE / START_MANY self-clear here.
            cs_ = (value & ~kCS_READY) & ~kCS_START_ONCE;
            if ((value & kCS_START_ONCE) != 0 && (cs_ & kCS_EN) != 0 && !converting_) {
                cs_ &= ~kCS_READY;
                converting_ = true;
                conv_countdown_ = 96u + ((div_ >> 8) & 0xFFFFu);  // same fixed SAR time as free-running
            }
            refresh_irq();
            break;
        }
        case kFCS: {
            const std::uint32_t sticky_mask = kFCS_ERR | kFCS_OVER | kFCS_UNDER;
            // config bits from the write, sticky bits kept unless written 1
            fcs_ = (value & ~sticky_mask) | (fcs_ & sticky_mask & ~(value & sticky_mask));
            refresh_irq();
            break;
        }
        case kDIV:  div_ = value; break;
        case kINTE: inte_ = value & 1u; refresh_irq(); break;
        case kINTF: intf_ = value & 1u; refresh_irq(); break;
        default: break;
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
