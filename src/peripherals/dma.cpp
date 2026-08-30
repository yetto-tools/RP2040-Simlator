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
constexpr unsigned      kCTRL_IRQ_QUIET = 21;
constexpr unsigned      kCTRL_BSWAP = 22;
constexpr std::uint32_t kCTRL_BUSY     = 1u << 24;
constexpr std::uint32_t kCTRL_WRITE_ERROR = 1u << 29;
constexpr std::uint32_t kCTRL_READ_ERROR  = 1u << 30;

// Shared-register offsets (from kBase + 0x400).
enum : std::uint32_t {
    kINTR = 0x400, kINTE0 = 0x404, kINTF0 = 0x408, kINTS0 = 0x40C,
    kINTE1 = 0x414, kINTF1 = 0x418, kINTS1 = 0x41C,
    kMULTI_CHAN_TRIGGER = 0x430,
    kCHAN_ABORT = 0x444, kN_CHANNELS = 0x448,
};

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

void Dma::run_transfer(unsigned ch) {
    Channel& c = chan_[ch];
    const std::uint32_t ctrl = c.ctrl;
    const unsigned size = 1u << ((ctrl >> kCTRL_DATA_SIZE_LSB) & 0x3u);  // 1/2/4/8->clamp
    const bool incr_read = ((ctrl >> kCTRL_INCR_READ) & 1u) != 0;
    const bool incr_write = ((ctrl >> kCTRL_INCR_WRITE) & 1u) != 0;
    const bool bswap = ((ctrl >> kCTRL_BSWAP) & 1u) != 0;
    const unsigned ring_size = (ctrl >> kCTRL_RING_SIZE_LSB) & 0xFu;
    const bool ring_on_write = ((ctrl >> kCTRL_RING_SEL) & 1u) != 0;
    const std::uint32_t ring_mask = ring_size != 0 ? ((1u << ring_size) - 1u) : 0u;
    const unsigned step = (size == 8) ? 4u : size;  // model 8-byte as two words? use 4

    for (std::uint32_t i = 0; i < c.trans_count; ++i) {
        std::uint32_t value = 0;
        BusStatus rs = BusStatus::Ok;
        switch (step) {
            case 1: { auto r = mem_.read_byte(c.read_addr); rs = r.status; value = r.value; break; }
            case 2: { auto r = mem_.read_half(c.read_addr); rs = r.status; value = r.value; break; }
            default: { auto r = mem_.read_word(c.read_addr); rs = r.status; value = r.value; break; }
        }
        if (rs != BusStatus::Ok) { c.ctrl |= kCTRL_READ_ERROR; break; }

        if (bswap) value = byteswap(value, step);

        BusStatus ws = BusStatus::Ok;
        switch (step) {
            case 1: ws = mem_.write_byte(c.write_addr, static_cast<std::uint8_t>(value)); break;
            case 2: ws = mem_.write_half(c.write_addr, static_cast<std::uint16_t>(value)); break;
            default: ws = mem_.write_word(c.write_addr, value); break;
        }
        if (ws != BusStatus::Ok) { c.ctrl |= kCTRL_WRITE_ERROR; break; }

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
    }
    c.trans_count = 0;
}

void Dma::trigger(unsigned ch) {
    if (ch >= kNumChannels) return;
    Channel& c = chan_[ch];
    if ((c.ctrl & kCTRL_EN) == 0) return;
    if (chain_depth_ > kNumChannels) return;  // runaway chain guard

    c.ctrl |= kCTRL_BUSY;
    run_transfer(ch);
    c.ctrl &= ~kCTRL_BUSY;

    if (((c.ctrl >> kCTRL_IRQ_QUIET) & 1u) == 0) {
        intr_ |= (1u << ch);
    }

    const unsigned chain_to = (c.ctrl >> kCTRL_CHAIN_TO_LSB) & 0xFu;
    refresh_irqs();
    if (chain_to != ch && chain_to < kNumChannels) {
        ++chain_depth_;
        trigger(chain_to);
        --chain_depth_;
    }
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
            case R_COUNT: return {c.trans_count, BusStatus::Ok};
            case R_CTRL:  return {c.ctrl, BusStatus::Ok};
        }
    }
    switch (offset) {
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
    switch (offset) {
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
