#include "peripherals/dma.h"

namespace rp2040 {

namespace {

// CTRL fields (datasheet 2.5.7).
constexpr std::uint32_t kCTRL_EN       = 1u << 0;
constexpr unsigned      kCTRL_DATA_SIZE_LSB = 2;   // [3:2]
constexpr unsigned      kCTRL_INCR_READ = 4;
constexpr unsigned      kCTRL_INCR_WRITE = 5;
constexpr unsigned      kCTRL_RING_SIZE_LSB = 6;   // [9:6]
constexpr unsigned      kCTRL_RING_SEL = 10;
constexpr unsigned      kCTRL_CHAIN_TO_LSB = 11;   // [14:11]
constexpr unsigned      kCTRL_TREQ_SEL_LSB = 15;   // [20:15]
constexpr unsigned      kCTRL_IRQ_QUIET = 21;
constexpr unsigned      kCTRL_BSWAP = 22;
constexpr std::uint32_t kCTRL_BUSY     = 1u << 24;
constexpr std::uint32_t kCTRL_WRITE_ERROR = 1u << 29;
constexpr std::uint32_t kCTRL_READ_ERROR  = 1u << 30;

constexpr std::uint32_t kTREQ_PERMANENT = 0x3Fu;
constexpr std::uint32_t kCTRL_SNIFF_EN = 1u << 23;

// Shared-register offsets (from kBase + 0x400).
enum : std::uint32_t {
    kINTR = 0x400, kINTE0 = 0x404, kINTF0 = 0x408, kINTS0 = 0x40C,
    kINTE1 = 0x414, kINTF1 = 0x418, kINTS1 = 0x41C,
    kTIMER0 = 0x420,  // TIMER0..3 at 0x420,0x424,0x428,0x42C
    kMULTI_CHAN_TRIGGER = 0x430,
    kSNIFF_CTRL = 0x434, kSNIFF_DATA = 0x438,
    kCHAN_ABORT = 0x444, kN_CHANNELS = 0x448,
};

// SNIFF_CTRL fields.
constexpr std::uint32_t kSNIFF_EN = 1u << 0;
constexpr unsigned      kSNIFF_DMACH_LSB = 1;   // [4:1]
constexpr unsigned      kSNIFF_CALC_LSB = 5;    // [8:5]
constexpr std::uint32_t kSNIFF_BSWAP = 1u << 9;
constexpr std::uint32_t kSNIFF_OUT_REV = 1u << 10;
constexpr std::uint32_t kSNIFF_OUT_INV = 1u << 11;

std::uint32_t bit_reverse32(std::uint32_t v) {
    v = ((v & 0xFFFF0000u) >> 16) | ((v & 0x0000FFFFu) << 16);
    v = ((v & 0xFF00FF00u) >> 8)  | ((v & 0x00FF00FFu) << 8);
    v = ((v & 0xF0F0F0F0u) >> 4)  | ((v & 0x0F0F0F0Fu) << 4);
    v = ((v & 0xCCCCCCCCu) >> 2)  | ((v & 0x33333333u) << 2);
    v = ((v & 0xAAAAAAAAu) >> 1)  | ((v & 0x55555555u) << 1);
    return v;
}

// Which of {READ, WRITE, COUNT, CTRL} each alias slot maps to.
enum Reg { R_READ, R_WRITE, R_COUNT, R_CTRL };
constexpr Reg kAliasMap[4][4] = {
    {R_READ, R_WRITE, R_COUNT, R_CTRL},   // alias 0: ..., CTRL_TRIG
    {R_CTRL, R_READ, R_WRITE, R_COUNT},   // alias 1: ..., TRANS_COUNT_TRIG
    {R_CTRL, R_COUNT, R_READ, R_WRITE},   // alias 2: ..., WRITE_ADDR_TRIG
    {R_CTRL, R_WRITE, R_COUNT, R_READ},   // alias 3: ..., READ_ADDR_TRIG
};

std::uint32_t byteswap(std::uint32_t v, unsigned size) {
    if (size == 2) return ((v & 0xFFu) << 8) | ((v >> 8) & 0xFFu);
    if (size == 4) return (v << 24) | ((v & 0xFF00u) << 8) | ((v >> 8) & 0xFF00u) | (v >> 24);
    return v;
}

}  // namespace

void Dma::refresh_irqs() {
    for (unsigned k = 0; k < 2; ++k) {
        const std::uint32_t mis = (intr_ | intf_[k]) & inte_[k];
        const unsigned irq = (k == 0) ? kIrq0 : kIrq1;
        if (mis != 0) nvic_.pend_exception(irq);
        else          nvic_.clear_pending(irq);
    }
}

Dma::Rate Dma::rate_for(const Channel& c) const {
    const std::uint32_t sel = (c.ctrl >> kCTRL_TREQ_SEL_LSB) & 0x3Fu;
    if (sel == kTREQ_PERMANENT) return {1, 1};
    if (sel >= 0x3Bu && sel <= 0x3Eu) {
        const std::uint32_t t = pacing_timer_[sel - 0x3Bu];
        const std::uint64_t x = t >> 16;
        const std::uint64_t y = t & 0xFFFFu;
        if (x == 0 || y == 0) return {0, 1};   // timer disabled -> never
        return {x, y};
    }
    return {1, dreq_divisor_};                 // a peripheral DREQ (approximated)
}

void Dma::sniff_feed(unsigned ch, std::uint32_t value, unsigned size) {
    if ((sniff_ctrl_ & kSNIFF_EN) == 0) return;
    if (((sniff_ctrl_ >> kSNIFF_DMACH_LSB) & 0xFu) != ch) return;
    if ((chan_[ch].ctrl & kCTRL_SNIFF_EN) == 0) return;

    const unsigned calc = (sniff_ctrl_ >> kSNIFF_CALC_LSB) & 0xFu;
    if (sniff_ctrl_ & kSNIFF_BSWAP) value = byteswap(value, size == 8 ? 4 : size);

    // Simple accumulators operate on the whole element.
    if (calc == 0xF) { sniff_data_ += value; return; }             // sum
    if (calc == 0xE) { sniff_data_ ^= value; return; }             // XOR reduction

    // The CRC modes fold the element in one byte at a time, LSB byte first.
    const unsigned nbytes = (size == 8) ? 4u : size;
    for (unsigned i = 0; i < nbytes; ++i) {
        const std::uint8_t b = static_cast<std::uint8_t>(value >> (8u * i));
        switch (calc) {
            case 0x0: {  // CRC-32, bit-normal, poly 0x04C11DB7
                sniff_data_ ^= static_cast<std::uint32_t>(b) << 24;
                for (int k = 0; k < 8; ++k)
                    sniff_data_ = (sniff_data_ & 0x80000000u)
                        ? (sniff_data_ << 1) ^ 0x04C11DB7u : (sniff_data_ << 1);
                break;
            }
            case 0x1: {  // CRC-32, bit-reversed, reflected poly 0xEDB88320
                sniff_data_ ^= b;
                for (int k = 0; k < 8; ++k)
                    sniff_data_ = (sniff_data_ & 1u)
                        ? (sniff_data_ >> 1) ^ 0xEDB88320u : (sniff_data_ >> 1);
                break;
            }
            case 0x2: {  // CRC-16-CCITT, bit-normal, poly 0x1021
                std::uint32_t c = sniff_data_ & 0xFFFFu;
                c ^= static_cast<std::uint32_t>(b) << 8;
                for (int k = 0; k < 8; ++k)
                    c = (c & 0x8000u) ? ((c << 1) ^ 0x1021u) : (c << 1);
                sniff_data_ = c & 0xFFFFu;
                break;
            }
            case 0x3: {  // CRC-16-CCITT, bit-reversed, reflected poly 0x8408
                std::uint32_t c = sniff_data_ & 0xFFFFu;
                c ^= b;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1u) ? ((c >> 1) ^ 0x8408u) : (c >> 1);
                sniff_data_ = c & 0xFFFFu;
                break;
            }
            default:
                break;  // reserved CALC value: no-op
        }
    }
}

std::uint32_t Dma::sniff_data_out() const {
    std::uint32_t v = sniff_data_;
    if (sniff_ctrl_ & kSNIFF_OUT_REV) v = bit_reverse32(v);
    if (sniff_ctrl_ & kSNIFF_OUT_INV) v = ~v;
    return v;
}

bool Dma::transfer_one(unsigned ch) {
    Channel& c = chan_[ch];
    const std::uint32_t ctrl = c.ctrl;
    const unsigned size = 1u << ((ctrl >> kCTRL_DATA_SIZE_LSB) & 0x3u);
    const bool incr_read = ((ctrl >> kCTRL_INCR_READ) & 1u) != 0;
    const bool incr_write = ((ctrl >> kCTRL_INCR_WRITE) & 1u) != 0;
    const bool bswap = ((ctrl >> kCTRL_BSWAP) & 1u) != 0;
    const unsigned ring_size = (ctrl >> kCTRL_RING_SIZE_LSB) & 0xFu;
    const bool ring_on_write = ((ctrl >> kCTRL_RING_SEL) & 1u) != 0;
    const std::uint32_t ring_mask = ring_size != 0 ? ((1u << ring_size) - 1u) : 0u;
    const unsigned step = (size == 8) ? 4u : size;  // 8-byte transfers modelled as words

    std::uint32_t value = 0;
    BusStatus rs = BusStatus::Ok;
    switch (step) {
        case 1: { auto r = mem_.read_byte(c.read_addr); rs = r.status; value = r.value; break; }
        case 2: { auto r = mem_.read_half(c.read_addr); rs = r.status; value = r.value; break; }
        default: { auto r = mem_.read_word(c.read_addr); rs = r.status; value = r.value; break; }
    }
    if (rs != BusStatus::Ok) { c.ctrl |= kCTRL_READ_ERROR; return false; }

    if (bswap) value = byteswap(value, step);

    BusStatus ws = BusStatus::Ok;
    switch (step) {
        case 1: ws = mem_.write_byte(c.write_addr, static_cast<std::uint8_t>(value)); break;
        case 2: ws = mem_.write_half(c.write_addr, static_cast<std::uint16_t>(value)); break;
        default: ws = mem_.write_word(c.write_addr, value); break;
    }
    if (ws != BusStatus::Ok) { c.ctrl |= kCTRL_WRITE_ERROR; return false; }

    sniff_feed(ch, value, step);

    if (incr_read) {
        if (ring_mask != 0 && !ring_on_write) {
            c.read_addr = (c.read_addr & ~ring_mask) | ((c.read_addr + step) & ring_mask);
        } else {
            c.read_addr += step;
        }
    }
    if (incr_write) {
        if (ring_mask != 0 && ring_on_write) {
            c.write_addr = (c.write_addr & ~ring_mask) | ((c.write_addr + step) & ring_mask);
        } else {
            c.write_addr += step;
        }
    }
    return true;
}

void Dma::trigger(unsigned ch) {
    if (ch >= kNumChannels) return;
    Channel& c = chan_[ch];
    if ((c.ctrl & kCTRL_EN) == 0) return;

    c.remaining = c.trans_count;
    c.pace_accum = 0;
    c.ctrl |= kCTRL_BUSY;
    c.ctrl &= ~(kCTRL_READ_ERROR | kCTRL_WRITE_ERROR);

    if (c.remaining == 0) {                 // a null transfer completes at once
        if (chain_depth_ <= kNumChannels) {
            ++chain_depth_;
            complete(ch, /*error=*/false);
            --chain_depth_;
        }
    }
}

void Dma::complete(unsigned ch, bool error) {
    Channel& c = chan_[ch];
    c.ctrl &= ~kCTRL_BUSY;
    c.remaining = 0;
    c.trans_count = 0;

    if (!error && ((c.ctrl >> kCTRL_IRQ_QUIET) & 1u) == 0) {
        intr_ |= (1u << ch);
    }
    refresh_irqs();

    if (error) return;
    const unsigned chain_to = (c.ctrl >> kCTRL_CHAIN_TO_LSB) & 0xFu;
    if (chain_to != ch && chain_to < kNumChannels) {
        trigger(chain_to);
    }
}

void Dma::on_cycles(std::uint64_t sys_cycles) {
    for (std::uint64_t t = 0; t < sys_cycles; ++t) {
        for (unsigned ch = 0; ch < kNumChannels; ++ch) {
            Channel& c = chan_[ch];
            if ((c.ctrl & kCTRL_BUSY) == 0 || c.remaining == 0) continue;

            const std::uint32_t sel = (c.ctrl >> kCTRL_TREQ_SEL_LSB) & 0x3Fu;
            if (sel < kNumDreqs && dreq_ready_[sel]) {
                // A real, registered DREQ source (datasheet 2.5.3.2): up to
                // one transfer per clock while the peripheral reports ready.
                if (!dreq_ready_[sel]()) continue;
                if (!transfer_one(ch)) { complete(ch, /*error=*/true); continue; }
                if (--c.remaining == 0) complete(ch, /*error=*/false);
                continue;
            }

            const Rate r = rate_for(c);
            if (r.num == 0) continue;                  // pacing source idle
            c.pace_accum += r.num;
            std::uint64_t budget = c.pace_accum / r.den;
            c.pace_accum %= r.den;

            while (budget-- > 0 && c.remaining > 0) {
                if (!transfer_one(ch)) { complete(ch, /*error=*/true); break; }
                if (--c.remaining == 0) { complete(ch, /*error=*/false); break; }
            }
        }
    }
}

void Dma::reset() {
    chan_.fill(Channel{});
    intr_ = 0;
    inte_.fill(0);
    intf_.fill(0);
    pacing_timer_.fill(0);
    sniff_ctrl_ = 0;
    sniff_data_ = 0;
    dreq_divisor_ = 2;
    chain_depth_ = 0;
    refresh_irqs();
    // dreq_ready_ is simulator wiring (set_dreq_source(), registered once by
    // Simulator at startup against each peripheral's live FIFO state) - not
    // DMA's own register state, left alone.
}

BusResult<std::uint32_t> Dma::reg_read(std::uint32_t offset, BusWidth) {
    if (offset < kNumChannels * 0x40u) {
        const unsigned ch = offset / 0x40u;
        const unsigned within = offset & 0x3Fu;
        const Reg reg = kAliasMap[within / 0x10u][(within % 0x10u) / 4u];
        const Channel& c = chan_[ch];
        switch (reg) {
            case R_READ:  return {c.read_addr, BusStatus::Ok};
            case R_WRITE: return {c.write_addr, BusStatus::Ok};
            case R_COUNT: return {(c.ctrl & kCTRL_BUSY) ? c.remaining : c.trans_count,
                                  BusStatus::Ok};
            case R_CTRL:  return {c.ctrl, BusStatus::Ok};
        }
    }
    if (offset >= kTIMER0 && offset < kTIMER0 + 4u * 4u) {
        return {pacing_timer_[(offset - kTIMER0) / 4u], BusStatus::Ok};
    }
    switch (offset) {
        case kSNIFF_CTRL: return {sniff_ctrl_, BusStatus::Ok};
        case kSNIFF_DATA: return {sniff_data_out(), BusStatus::Ok};
        case kINTR:  return {intr_, BusStatus::Ok};
        case kINTE0: return {inte_[0], BusStatus::Ok};
        case kINTF0: return {intf_[0], BusStatus::Ok};
        case kINTS0: return {(intr_ | intf_[0]) & inte_[0], BusStatus::Ok};
        case kINTE1: return {inte_[1], BusStatus::Ok};
        case kINTF1: return {intf_[1], BusStatus::Ok};
        case kINTS1: return {(intr_ | intf_[1]) & inte_[1], BusStatus::Ok};
        case kN_CHANNELS: return {kNumChannels, BusStatus::Ok};
        default: return {0u, BusStatus::Ok};
    }
}

BusStatus Dma::reg_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    if (offset < kNumChannels * 0x40u) {
        const unsigned ch = offset / 0x40u;
        const unsigned within = offset & 0x3Fu;
        const unsigned slot = (within % 0x10u) / 4u;
        const Reg reg = kAliasMap[within / 0x10u][slot];
        Channel& c = chan_[ch];
        switch (reg) {
            case R_READ:  c.read_addr = value; break;
            case R_WRITE: c.write_addr = value; break;
            case R_COUNT: c.trans_count = value; break;
            case R_CTRL:  c.ctrl = value & ~kCTRL_BUSY; break;
        }
        if (slot == 3) trigger(ch);  // the last register of each alias is the trigger
        return BusStatus::Ok;
    }
    if (offset >= kTIMER0 && offset < kTIMER0 + 4u * 4u) {
        pacing_timer_[(offset - kTIMER0) / 4u] = value;
        return BusStatus::Ok;
    }
    switch (offset) {
        case kSNIFF_CTRL: sniff_ctrl_ = value & 0xFFFu; break;
        case kSNIFF_DATA: sniff_data_ = value; break;   // seed / clear
        case kINTR:
            intr_ &= ~value;                 // write-1-clear
            refresh_irqs();
            break;
        case kINTE0: inte_[0] = value & 0xFFFu; refresh_irqs(); break;
        case kINTF0: intf_[0] = value & 0xFFFu; refresh_irqs(); break;
        case kINTE1: inte_[1] = value & 0xFFFu; refresh_irqs(); break;
        case kINTF1: intf_[1] = value & 0xFFFu; refresh_irqs(); break;
        case kMULTI_CHAN_TRIGGER:
            for (unsigned ch = 0; ch < kNumChannels; ++ch)
                if ((value >> ch) & 1u) trigger(ch);
            break;
        case kCHAN_ABORT:
            for (unsigned ch = 0; ch < kNumChannels; ++ch)
                if ((value >> ch) & 1u) {
                    chan_[ch].remaining = 0;
                    chan_[ch].trans_count = 0;
                    chan_[ch].ctrl &= ~kCTRL_BUSY;
                }
            break;
        default:
            break;
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
