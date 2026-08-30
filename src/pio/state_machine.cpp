#include "pio/state_machine.h"

namespace rp2040 {

namespace {

std::uint32_t mask_n(unsigned n) {
    return n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u);
}

unsigned wrap_pin(unsigned p) { return p % static_cast<unsigned>(Gpio::kNumPins); }

std::uint32_t bit_reverse(std::uint32_t v) {
    v = ((v & 0xAAAAAAAAu) >> 1) | ((v & 0x55555555u) << 1);
    v = ((v & 0xCCCCCCCCu) >> 2) | ((v & 0x33333333u) << 2);
    v = ((v & 0xF0F0F0F0u) >> 4) | ((v & 0x0F0F0F0Fu) << 4);
    v = ((v & 0xFF00FF00u) >> 8) | ((v & 0x00FF00FFu) << 8);
    return (v >> 16) | (v << 16);
}

}  // namespace

void StateMachine::restart() {
    pc = cfg.wrap_bottom;
    osr = 0;
    isr = 0;
    osr_shift_count = 32;   // OSR empty
    isr_shift_count = 0;    // ISR empty
    delay_left_ = 0;
    stall_ = Stall::None;
    irq_wait_raised_ = false;
}

unsigned StateMachine::delay_of(const PioInstr& in) const {
    unsigned ss_bits = cfg.sideset_count + (cfg.sideset_opt ? 1u : 0u);
    if (ss_bits >= 5) return 0;
    const unsigned delay_bits = 5u - ss_bits;
    return in.delay_sideset & ((1u << delay_bits) - 1u);
}

bool StateMachine::do_autopull() {
    if (osr_shift_count < pull_thresh()) return true;  // OSR still has data
    std::uint32_t v = 0;
    if (!tx.pop(v)) return false;                      // TX empty -> caller stalls
    osr = v;
    osr_shift_count = 0;
    return true;
}

void StateMachine::maybe_autopush() {
    if (!cfg.autopush || isr_shift_count < push_thresh()) return;
    if (rx.push(isr)) {
        isr = 0;
        isr_shift_count = 0;
    } else {
        stall_ = Stall::AutoPush;  // retry the push next tick, do not advance
    }
}

void StateMachine::advance_pc(const PioInstr& in) {
    if (pc == cfg.wrap_top) {
        pc = cfg.wrap_bottom;
    } else {
        pc = static_cast<std::uint8_t>((pc + 1) & 0x1Fu);
    }
    (void)in;
}

std::uint32_t StateMachine::read_pins(std::uint8_t base, unsigned count) const {
    std::uint32_t v = 0;
    if (gpio_ == nullptr) return 0;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned pin = wrap_pin(base + i);
        if (gpio_->level(pin)) v |= (1u << i);
    }
    return v;
}

void StateMachine::write_pins(std::uint8_t base, unsigned count, std::uint32_t value, bool dirs) {
    if (gpio_ == nullptr) return;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned pin = wrap_pin(base + i);
        const bool bit = ((value >> i) & 1u) != 0;
        if (dirs) gpio_->driver_set_pindir(driver_, pin, bit);
        else      gpio_->driver_set_pin(driver_, pin, bit);
    }
}

unsigned StateMachine::resolve_irq(std::uint8_t index) const {
    if ((index & 0x10u) != 0) {  // "relative" - add the SM id, mod 4, within a group of 4
        return (index & 0x4u) | (((index & 0x3u) + sm_id_) & 0x3u);
    }
    return index & 0x7u;
}

void StateMachine::apply_sideset(const PioInstr& in) {
    if (cfg.sideset_count == 0 || gpio_ == nullptr) return;
    const unsigned total = cfg.sideset_count + (cfg.sideset_opt ? 1u : 0u);
    const unsigned delay_bits = (total >= 5) ? 0u : (5u - total);
    if (cfg.sideset_opt && ((in.delay_sideset >> 4) & 1u) == 0) return;  // side-set not present

    const std::uint32_t ss = (in.delay_sideset >> delay_bits) & ((1u << cfg.sideset_count) - 1u);
    write_pins(cfg.sideset_base, cfg.sideset_count, ss, cfg.sideset_pindir);
}

bool StateMachine::exec(const PioInstr& in) {
    apply_sideset(in);
    switch (in.op) {
        case PioOp::JMP: {
            bool take = false;
            switch (in.condition) {
                case kJmpAlways:  take = true; break;
                case kJmpNotX:    take = (x == 0); break;
                case kJmpXDec:    take = (x != 0); x = x - 1; break;
                case kJmpNotY:    take = (y == 0); break;
                case kJmpYDec:    take = (y != 0); y = y - 1; break;
                case kJmpXNeY:    take = (x != y); break;
                case kJmpPin:    take = (gpio_ != nullptr) && gpio_->level(cfg.jmp_pin); break;
                case kJmpNotOsrE: take = (osr_shift_count < pull_thresh()); break;
                default: break;
            }
            if (take) { pc = static_cast<std::uint8_t>(in.address & 0x1Fu); return true; }
            advance_pc(in);
            return true;
        }

        case PioOp::SET: {
            const std::uint32_t d = in.data & 0x1Fu;
            switch (in.destination) {
                case kSetX: x = d; break;
                case kSetY: y = d; break;
                case kSetPins:    write_pins(cfg.set_base, cfg.set_count, d, /*dirs=*/false); break;
                case kSetPindirs: write_pins(cfg.set_base, cfg.set_count, d, /*dirs=*/true); break;
                default: break;
            }
            advance_pc(in);
            return true;
        }

        case PioOp::MOV: {
            std::uint32_t src = 0;
            switch (in.source) {
                case kMovX: src = x; break;
                case kMovY: src = y; break;
                case kMovNull: src = 0; break;
                case kMovIsr: src = isr; break;
                case kMovOsr: src = osr; break;
                case kMovPins: src = read_pins(cfg.in_base, 32); break;
                case kMovStatus: src = 0; break;   // FIFO-level status: deferred
                default: break;
            }
            switch (in.mov_op) {
                case kMovInvert: src = ~src; break;
                case kMovBitRev: src = bit_reverse(src); break;
                default: break;
            }
            switch (in.destination) {
                case kMovX: x = src; break;
                case kMovY: y = src; break;
                case kMovIsr: isr = src; isr_shift_count = 0; break;
                case kMovOsr: osr = src; osr_shift_count = 0; break;
                case kMovPins: write_pins(cfg.out_base, cfg.out_count, src, /*dirs=*/false); break;
                case 5 /*EXEC*/: break;            // MOV EXEC: deferred
                default: break;
            }
            advance_pc(in);
            return true;
        }

        case PioOp::IN: {
            const unsigned n = in.bit_count;
            std::uint32_t src = 0;
            switch (in.source) {
                case kInX: src = x; break;
                case kInY: src = y; break;
                case kInNull: src = 0; break;
                case kInIsr: src = isr; break;
                case kInOsr: src = osr; break;
                case kInPins: src = read_pins(cfg.in_base, n); break;
                default: break;
            }
            src &= mask_n(n);
            if (cfg.in_shiftdir_right) {
                isr = (n >= 32) ? src : ((isr >> n) | (src << (32u - n)));
            } else {
                isr = (n >= 32) ? src : ((isr << n) | src);
            }
            isr_shift_count = isr_shift_count + n;
            if (isr_shift_count > 32) isr_shift_count = 32;
            maybe_autopush();
            if (stall_ == Stall::AutoPush) return false;
            advance_pc(in);
            return true;
        }

        case PioOp::OUT: {
            if (cfg.autopull && !do_autopull()) {
                stall_ = Stall::AutoPull;
                return false;
            }
            const unsigned n = in.bit_count;
            std::uint32_t out_val;
            if (cfg.out_shiftdir_right) {
                out_val = osr & mask_n(n);
                osr = (n >= 32) ? 0u : (osr >> n);
            } else {
                out_val = (n >= 32) ? osr : (osr >> (32u - n));
                osr = (n >= 32) ? 0u : (osr << n);
            }
            osr_shift_count = osr_shift_count + n;
            if (osr_shift_count > 32) osr_shift_count = 32;

            switch (in.destination) {
                case kOutX: x = out_val; break;
                case kOutY: y = out_val; break;
                case kOutNull: break;
                case kOutPc: pc = static_cast<std::uint8_t>(out_val & 0x1Fu); return true;
                case kOutIsr: isr = out_val; isr_shift_count = n; break;
                case kOutPins:    write_pins(cfg.out_base, cfg.out_count, out_val, /*dirs=*/false); break;
                case kOutPindirs: write_pins(cfg.out_base, cfg.out_count, out_val, /*dirs=*/true); break;
                case kOutExec: break;                    // OUT EXEC: deferred
                default: break;
            }
            advance_pc(in);
            return true;
        }

        case PioOp::PUSH: {
            if (in.if_full && isr_shift_count < push_thresh()) {
                advance_pc(in);
                return true;  // iffull not reached -> no-op
            }
            if (rx.push(isr)) {
                isr = 0;
                isr_shift_count = 0;
                advance_pc(in);
                return true;
            }
            if (in.block) return false;   // stall (Stall::Instr set by caller)
            // non-blocking, FIFO full: data lost, ISR still cleared
            isr = 0;
            isr_shift_count = 0;
            advance_pc(in);
            return true;
        }

        case PioOp::PULL: {
            if (in.if_empty && osr_shift_count < pull_thresh()) {
                advance_pc(in);
                return true;  // ifempty not reached -> no-op
            }
            std::uint32_t v = 0;
            if (tx.pop(v)) {
                osr = v;
                osr_shift_count = 0;
                advance_pc(in);
                return true;
            }
            if (in.block) return false;   // stall
            // non-blocking, FIFO empty: OSR <- X (datasheet 3.4.9)
            osr = x;
            osr_shift_count = 0;
            advance_pc(in);
            return true;
        }

        case PioOp::WAIT: {
            bool satisfied = false;
            unsigned wait_irq = 0;
            switch (in.source) {
                case kWaitGpio:
                    satisfied = (gpio_ != nullptr) &&
                                (gpio_->level(wrap_pin(in.index)) == in.polarity);
                    break;
                case kWaitPin:
                    satisfied = (gpio_ != nullptr) &&
                                (gpio_->level(wrap_pin(static_cast<unsigned>(cfg.in_base) + in.index)) == in.polarity);
                    break;
                case kWaitIrq: {
                    wait_irq = resolve_irq(in.index);
                    const bool flag = (block_irq_ != nullptr) &&
                                      ((*block_irq_ >> wait_irq) & 1u) != 0;
                    satisfied = (flag == in.polarity);
                    break;
                }
                default: satisfied = true; break;
            }
            if (!satisfied) return false;  // stall, retry next cycle
            if (in.source == kWaitIrq && in.polarity && block_irq_ != nullptr) {
                *block_irq_ = static_cast<std::uint8_t>(*block_irq_ & ~(1u << wait_irq));
            }
            advance_pc(in);
            return true;
        }

        case PioOp::IRQ: {
            const unsigned n = resolve_irq(in.index);
            if (block_irq_ == nullptr) { advance_pc(in); return true; }

            if (in.clear) {
                *block_irq_ = static_cast<std::uint8_t>(*block_irq_ & ~(1u << n));
                advance_pc(in);
                return true;
            }
            // Set. Raise the flag once; if Wait, stall until it is lowered
            // again by another entity (do not re-raise it on the retry).
            if (!irq_wait_raised_) {
                *block_irq_ = static_cast<std::uint8_t>(*block_irq_ | (1u << n));
                irq_wait_raised_ = in.wait;
            }
            if (in.wait && ((*block_irq_ >> n) & 1u) != 0) {
                return false;  // still stalled
            }
            irq_wait_raised_ = false;
            advance_pc(in);
            return true;
        }
    }
    advance_pc(in);
    return true;
}

StateMachine::TickOutcome StateMachine::tick() {
    if (!enabled_ || program_ == nullptr) return {};

    if (delay_left_ > 0) {
        --delay_left_;
        return {false, false, true};
    }

    // Resolve a pending autopush / autopull from the previous instruction.
    if (stall_ == Stall::AutoPush) {
        if (!rx.push(isr)) return {false, true, false};
        isr = 0;
        isr_shift_count = 0;
        stall_ = Stall::None;
        advance_pc(cur_);
        delay_left_ = delay_of(cur_);
        return {true, false, false};
    }
    if (stall_ == Stall::AutoPull) {
        std::uint32_t v = 0;
        if (!tx.pop(v)) return {false, true, false};
        osr = v;
        osr_shift_count = 0;
        stall_ = Stall::None;
        // The stalled OUT still has to run its shift; re-execute it.
        stall_ = Stall::Instr;
    }

    // Proactive autopull: refill an empty OSR between instructions.
    if (stall_ != Stall::Instr && cfg.autopull && osr_shift_count >= pull_thresh()) {
        std::uint32_t v = 0;
        if (tx.pop(v)) {
            osr = v;
            osr_shift_count = 0;
        }
        // If TX is empty we simply proceed; the next OUT will stall.
    }

    const PioInstr in = (stall_ == Stall::Instr) ? cur_ : pio_decode(program_[pc & 0x1Fu]);
    cur_ = in;

    const bool ok = exec(in);
    if (!ok) {
        if (stall_ == Stall::None) stall_ = Stall::Instr;
        return {false, true, false};
    }
    if (stall_ == Stall::AutoPush || stall_ == Stall::AutoPull) {
        return {false, true, false};
    }

    stall_ = Stall::None;
    delay_left_ = delay_of(in);
    return {true, false, false};
}

}  // namespace rp2040
