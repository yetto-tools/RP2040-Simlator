#include "pio/pio_registers.h"

namespace rp2040 {

namespace {

enum : std::uint32_t {
    kCTRL = 0x000, kFSTAT = 0x004, kFDEBUG = 0x008, kFLEVEL = 0x00C,
    kTXF0 = 0x010,  // TXF0..TXF3 at +0,4,8,C
    kRXF0 = 0x020,  // RXF0..RXF3
    kIRQ = 0x030, kIRQ_FORCE = 0x034,
    kINSTR_MEM0 = 0x048,  // 32 words, +4 each -> 0x048..0x0C4
    kSM0_CLKDIV = 0x0C8,  // per-SM block is 0x18 bytes: CLKDIV, EXECCTRL,
                          // SHIFTCTRL, ADDR(ro), INSTR, PINCTRL
    kINTR = 0x128, kIRQ0_INTE = 0x12C, kIRQ0_INTF = 0x130, kIRQ0_INTS = 0x134,
    kIRQ1_INTE = 0x138, kIRQ1_INTF = 0x13C, kIRQ1_INTS = 0x140,
};

std::uint32_t field(std::uint32_t v, unsigned lo, unsigned width) {
    return (v >> lo) & ((1u << width) - 1u);
}

}  // namespace

void PioRegisters::write_ctrl(std::uint32_t value) {
    ctrl_ = value & 0xFFFu;
    for (unsigned i = 0; i < PioBlock::kNumSm; ++i) {
        block_.sm(i).set_enabled(((value >> i) & 1u) != 0);
        if (((value >> (4 + i)) & 1u) != 0) block_.sm(i).restart();
        if (((value >> (8 + i)) & 1u) != 0) block_.restart_clkdiv(i);
    }
}

void PioRegisters::write_sm_clkdiv(unsigned sm, std::uint32_t value) {
    clkdiv_[sm] = value;
    block_.set_clkdiv(sm, static_cast<std::uint16_t>(field(value, 16, 16)),
                      static_cast<std::uint8_t>(field(value, 8, 8)));
}

void PioRegisters::write_sm_execctrl(unsigned sm, std::uint32_t value) {
    execctrl_[sm] = value;
    SmConfig& c = block_.sm(sm).cfg;
    c.wrap_bottom = static_cast<std::uint8_t>(field(value, 7, 5));
    c.wrap_top = static_cast<std::uint8_t>(field(value, 12, 5));
    c.jmp_pin = static_cast<std::uint8_t>(field(value, 24, 5));
    c.sideset_pindir = ((value >> 29) & 1u) != 0;
    c.sideset_opt = ((value >> 30) & 1u) != 0;
}

void PioRegisters::write_sm_shiftctrl(unsigned sm, std::uint32_t value) {
    shiftctrl_[sm] = value;
    SmConfig& c = block_.sm(sm).cfg;
    c.autopush = ((value >> 16) & 1u) != 0;
    c.autopull = ((value >> 17) & 1u) != 0;
    c.in_shiftdir_right = ((value >> 18) & 1u) != 0;
    c.out_shiftdir_right = ((value >> 19) & 1u) != 0;
    const std::uint32_t push_t = field(value, 20, 5);
    const std::uint32_t pull_t = field(value, 25, 5);
    c.push_threshold = static_cast<std::uint8_t>(push_t == 0 ? 32u : push_t);
    c.pull_threshold = static_cast<std::uint8_t>(pull_t == 0 ? 32u : pull_t);
    const bool fjoin_tx = ((value >> 30) & 1u) != 0;
    const bool fjoin_rx = ((value >> 31) & 1u) != 0;
    block_.sm(sm).tx.set_depth(fjoin_tx ? PioFifo::kJoinedDepth
                                        : (fjoin_rx ? 0u : PioFifo::kDefaultDepth));
    block_.sm(sm).rx.set_depth(fjoin_rx ? PioFifo::kJoinedDepth
                                        : (fjoin_tx ? 0u : PioFifo::kDefaultDepth));
}

void PioRegisters::write_sm_pinctrl(unsigned sm, std::uint32_t value) {
    pinctrl_[sm] = value;
    SmConfig& c = block_.sm(sm).cfg;
    c.out_base = static_cast<std::uint8_t>(field(value, 0, 5));
    c.set_base = static_cast<std::uint8_t>(field(value, 5, 5));
    c.sideset_base = static_cast<std::uint8_t>(field(value, 10, 5));
    c.in_base = static_cast<std::uint8_t>(field(value, 15, 5));
    c.out_count = static_cast<std::uint8_t>(field(value, 20, 6));
    c.set_count = static_cast<std::uint8_t>(field(value, 26, 3));
    c.sideset_count = static_cast<std::uint8_t>(field(value, 29, 3));
}

std::uint32_t PioRegisters::read_fstat() const {
    std::uint32_t v = 0;
    for (unsigned i = 0; i < PioBlock::kNumSm; ++i) {
        if (block_.sm(i).tx.empty()) v |= (1u << (24 + i));
        if (block_.sm(i).tx.full())  v |= (1u << (16 + i));
        if (block_.sm(i).rx.empty()) v |= (1u << (8 + i));
        if (block_.sm(i).rx.full())  v |= (1u << i);
    }
    return v;
}

std::uint32_t PioRegisters::compute_intr() const {
    // datasheet 3.7 INTR: [3:0] SMx_RXNEMPTY, [7:4] SMx_TXNFULL, [11:8] SM IRQ 0-3.
    std::uint32_t v = 0;
    for (unsigned i = 0; i < PioBlock::kNumSm; ++i) {
        if (!block_.sm(i).rx.empty()) v |= (1u << i);
        if (!block_.sm(i).tx.full())  v |= (1u << (4 + i));
    }
    v |= (static_cast<std::uint32_t>(block_.irq()) & 0xFu) << 8;
    return v;
}

void PioRegisters::poll_interrupts() {
    if (cpu_ == nullptr) return;
    const std::uint32_t intr = compute_intr();
    const std::uint32_t mis0 = (intr | irq0_intf_) & irq0_inte_;
    const std::uint32_t mis1 = (intr | irq1_intf_) & irq1_inte_;
    if (mis0 != 0) cpu_->pend_exception(nvic_irq0_);     else cpu_->clear_pending(nvic_irq0_);
    if (mis1 != 0) cpu_->pend_exception(nvic_irq0_ + 1); else cpu_->clear_pending(nvic_irq0_ + 1);
}

std::uint32_t PioRegisters::read_flevel() const {
    std::uint32_t v = 0;
    for (unsigned i = 0; i < PioBlock::kNumSm; ++i) {
        v |= (block_.sm(i).tx.level() & 0xFu) << (8 * i);
        v |= (block_.sm(i).rx.level() & 0xFu) << (8 * i + 4);
    }
    return v;
}

BusResult<std::uint32_t> PioRegisters::bus_read(std::uint32_t offset, BusWidth) {
    if (offset >= kTXF0 && offset < kTXF0 + 16) {
        return {0u, BusStatus::Ok};  // TXF is write-only; reads as 0
    }
    if (offset >= kRXF0 && offset < kRXF0 + 16) {
        const unsigned sm = (offset - kRXF0) / 4u;
        std::uint32_t v = 0;
        block_.sm(sm).rx.pop(v);  // underflow returns stale 0 (FDEBUG bit not modelled)
        return {v, BusStatus::Ok};
    }
    if (offset >= kINSTR_MEM0 && offset < kINSTR_MEM0 + 32 * 4) {
        return {block_.instruction((offset - kINSTR_MEM0) / 4u), BusStatus::Ok};
    }
    if (offset >= kSM0_CLKDIV && offset < kSM0_CLKDIV + PioBlock::kNumSm * 0x18u) {
        const unsigned sm = (offset - kSM0_CLKDIV) / 0x18u;
        switch ((offset - kSM0_CLKDIV) % 0x18u) {
            case 0x00: return {clkdiv_[sm], BusStatus::Ok};
            case 0x04: return {execctrl_[sm], BusStatus::Ok};
            case 0x08: return {shiftctrl_[sm], BusStatus::Ok};
            case 0x0C: return {block_.sm(sm).pc, BusStatus::Ok};                 // ADDR (ro)
            case 0x10: return {block_.sm(sm).current_instruction(), BusStatus::Ok};
            case 0x14: return {pinctrl_[sm], BusStatus::Ok};
            default: return {0u, BusStatus::Ok};
        }
    }

    switch (offset) {
        case kCTRL:   return {ctrl_, BusStatus::Ok};
        case kFSTAT:  return {read_fstat(), BusStatus::Ok};
        case kFDEBUG: return {0u, BusStatus::Ok};
        case kFLEVEL: return {read_flevel(), BusStatus::Ok};
        case kIRQ:    return {block_.irq(), BusStatus::Ok};
        case kINTR:   return {compute_intr(), BusStatus::Ok};
        case kIRQ0_INTE: return {irq0_inte_, BusStatus::Ok};
        case kIRQ0_INTF: return {irq0_intf_, BusStatus::Ok};
        case kIRQ0_INTS: return {(compute_intr() | irq0_intf_) & irq0_inte_, BusStatus::Ok};
        case kIRQ1_INTE: return {irq1_inte_, BusStatus::Ok};
        case kIRQ1_INTF: return {irq1_intf_, BusStatus::Ok};
        case kIRQ1_INTS: return {(compute_intr() | irq1_intf_) & irq1_inte_, BusStatus::Ok};
        default: return {0u, BusStatus::Ok};
    }
}

BusStatus PioRegisters::bus_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    if (offset >= kTXF0 && offset < kTXF0 + 16) {
        const unsigned sm = (offset - kTXF0) / 4u;
        block_.sm(sm).tx.push(value);  // overflow drops (FDEBUG bit not modelled)
        return BusStatus::Ok;
    }
    if (offset >= kINSTR_MEM0 && offset < kINSTR_MEM0 + 32 * 4) {
        block_.write_instruction((offset - kINSTR_MEM0) / 4u,
                                 static_cast<std::uint16_t>(value & 0xFFFFu));
        return BusStatus::Ok;
    }
    if (offset >= kSM0_CLKDIV && offset < kSM0_CLKDIV + PioBlock::kNumSm * 0x18u) {
        const unsigned sm = (offset - kSM0_CLKDIV) / 0x18u;
        switch ((offset - kSM0_CLKDIV) % 0x18u) {
            case 0x00: write_sm_clkdiv(sm, value); break;
            case 0x04: write_sm_execctrl(sm, value); break;
            case 0x08: write_sm_shiftctrl(sm, value); break;
            case 0x10: block_.sm(sm).exec_immediate(static_cast<std::uint16_t>(value & 0xFFFFu)); break;
            case 0x14: write_sm_pinctrl(sm, value); break;
            default: break;  // ADDR is read-only
        }
        return BusStatus::Ok;
    }

    switch (offset) {
        case kCTRL: write_ctrl(value); break;
        case kFDEBUG: break;  // write-1-clear bits not modelled
        case kIRQ:
            for (unsigned n = 0; n < 8; ++n)
                if ((value >> n) & 1u) block_.set_irq(n, false);  // write-1-clear
            break;
        case kIRQ_FORCE:
            for (unsigned n = 0; n < 8; ++n)
                if ((value >> n) & 1u) block_.set_irq(n, true);
            break;
        case kIRQ0_INTE: irq0_inte_ = value & 0xFFFu; break;
        case kIRQ0_INTF: irq0_intf_ = value & 0xFFFu; break;
        case kIRQ1_INTE: irq1_inte_ = value & 0xFFFu; break;
        case kIRQ1_INTF: irq1_intf_ = value & 0xFFFu; break;
        default: break;
    }
    poll_interrupts();
    return BusStatus::Ok;
}

}  // namespace rp2040
