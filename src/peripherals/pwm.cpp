#include "peripherals/pwm.h"

namespace rp2040 {

namespace {
constexpr std::uint32_t kSliceStride = 0x14;  // CSR, DIV, CTR, CC, TOP
enum : std::uint32_t {
    kEN = 0xA0, kINTR = 0xA4, kINTE = 0xA8, kINTF = 0xAC, kINTS = 0xB0,
};
constexpr std::uint32_t kCSR_EN = 1u << 0;
constexpr std::uint32_t kCSR_PH_CORRECT = 1u << 1;
constexpr std::uint32_t kCSR_A_INV = 1u << 2;
constexpr std::uint32_t kCSR_B_INV = 1u << 3;
constexpr unsigned      kCSR_DIVMODE_LSB = 4;   // [5:4]
constexpr std::uint32_t kCSR_PH_ADV = 1u << 6;  // self-clearing strobe, never stored
constexpr std::uint32_t kCSR_PH_RET = 1u << 7;  // self-clearing strobe, never stored

// DIVMODE (datasheet 4.5.2.1): FREE runs off the fractional divider as
// normal; LEVEL additionally gates that same divider's clock on the B pin
// reading high; RISE/FALL bypass the divider entirely and advance the
// counter once per detected edge on B instead. In every mode but FREE, the
// B pin is repurposed as an input (its own PWM output driver is disabled),
// matching real hardware.
enum : unsigned { kDivFree = 0, kDivLevel = 1, kDivRise = 2, kDivFall = 3 };
}  // namespace

void Pwm::update_outputs(unsigned s) {
    const Slice& sl = slice_[s];
    const bool b_is_output = ((sl.csr >> kCSR_DIVMODE_LSB) & 0x3u) == kDivFree;
    const bool a_hi = (sl.ctr < sl.cc_a) ^ ((sl.csr & kCSR_A_INV) != 0);
    const bool b_hi = b_is_output && ((sl.ctr < sl.cc_b) ^ ((sl.csr & kCSR_B_INV) != 0));
    const unsigned pin_a = 2u * s;
    const unsigned pin_b = 2u * s + 1u;

    gpio_.driver_set_pindir(Gpio::kPwm, pin_a, true);
    gpio_.driver_set_pin(Gpio::kPwm, pin_a, a_hi);
    gpio_.driver_set_pindir(Gpio::kPwm, pin_a + 16u, true);
    gpio_.driver_set_pin(Gpio::kPwm, pin_a + 16u, a_hi);

    // B's OE tracks DIVMODE: an output in FREE mode, an input (OE low, so
    // Gpio::level() falls through to whatever's actually driving the pin)
    // in LEVEL/RISE/FALL, where B is the gate/edge source instead.
    gpio_.driver_set_pindir(Gpio::kPwm, pin_b, b_is_output);
    gpio_.driver_set_pin(Gpio::kPwm, pin_b, b_hi);
    gpio_.driver_set_pindir(Gpio::kPwm, pin_b + 16u, b_is_output);
    gpio_.driver_set_pin(Gpio::kPwm, pin_b + 16u, b_hi);
}

// One count backward - the exact mirror of advance_slice()'s single-step
// state machine, for CSR.PH_RET (datasheet 4.5.2.1: "retard the phase of
// the counter by 1 count, while it is running"). Doesn't raise the wrap
// IRQ: this is a manual correction for phase-aligning independently
// started slices, not a real wrap event.
void Pwm::retard_slice(unsigned s) {
    Slice& sl = slice_[s];
    const bool ph_correct = (sl.csr & kCSR_PH_CORRECT) != 0;
    if (!ph_correct) {
        if (sl.ctr == 0) sl.ctr = sl.top;
        else --sl.ctr;
    } else {
        if (!sl.counting_down) {
            if (sl.ctr == 0) { sl.counting_down = true; ++sl.ctr; }
            else --sl.ctr;
        } else {
            if (sl.ctr >= sl.top) { sl.counting_down = false; --sl.ctr; }
            else ++sl.ctr;
        }
    }
    update_outputs(s);
}

// Advance one slice's counter by a single PWM clock (on_cycles paces this
// via the fractional divider).
void Pwm::advance_slice(unsigned s) {
    Slice& sl = slice_[s];
    const bool ph_correct = (sl.csr & kCSR_PH_CORRECT) != 0;
    if (!ph_correct) {
        if (sl.ctr >= sl.top) {
            sl.ctr = 0;
            intr_ |= (1u << s);
        } else {
            ++sl.ctr;
        }
    } else {
        if (!sl.counting_down) {
            if (sl.ctr >= sl.top) { sl.counting_down = true; --sl.ctr; }
            else ++sl.ctr;
        } else {
            if (sl.ctr == 0) {
                sl.counting_down = false;
                intr_ |= (1u << s);
                ++sl.ctr;             // turn around: next clock is 1
            } else {
                --sl.ctr;
            }
        }
    }
    update_outputs(s);
}

void Pwm::refresh_irq() {
    const std::uint32_t mis = (intr_ | intf_) & inte_;
    if (mis != 0) nvic_.pend_exception(kIrqWrap);
    else          nvic_.clear_pending(kIrqWrap);
}

void Pwm::on_cycles(std::uint64_t sys_cycles) {
    for (std::uint64_t i = 0; i < sys_cycles; ++i) {
        for (unsigned s = 0; s < kNumSlices; ++s) {
            Slice& sl = slice_[s];
            if ((enable_ & (1u << s)) == 0 && (sl.csr & kCSR_EN) == 0) continue;
            const unsigned mode = (sl.csr >> kCSR_DIVMODE_LSB) & 0x3u;

            if (mode == kDivRise || mode == kDivFall) {
                // Datasheet 4.5.2.1: the fractional divider is bypassed
                // entirely - one edge on B is one count, full stop.
                const bool b = gpio_.func_level(2u * s + 1u);
                const bool edge = (mode == kDivRise) ? (b && !sl.prev_b) : (!b && sl.prev_b);
                sl.prev_b = b;
                if (edge) advance_slice(s);
                continue;
            }
            if (mode == kDivLevel && !gpio_.func_level(2u * s + 1u)) {
                continue;  // B low: the counter's clock is held, not just un-paced
            }

            // Pace by the integer divider: one counter step every div_int clocks.
            sl.frac_accum += 16u + (sl.div & 0xFu);
            const unsigned div_int = (sl.div >> 4) & 0xFFu;
            const unsigned period = (div_int == 0 ? 1u : div_int) * 16u;
            if (sl.frac_accum >= period) {
                sl.frac_accum -= period;
                advance_slice(s);
            }
        }
    }
    refresh_irq();
}

void Pwm::reset() {
    slice_.fill(Slice{});
    enable_ = 0;
    intr_ = 0;
    inte_ = 0;
    intf_ = 0;
    for (unsigned s = 0; s < kNumSlices; ++s) update_outputs(s);
    refresh_irq();
}

BusResult<std::uint32_t> Pwm::reg_read(std::uint32_t offset, BusWidth) {
    if (offset < kNumSlices * kSliceStride) {
        const unsigned s = offset / kSliceStride;
        const Slice& sl = slice_[s];
        switch (offset % kSliceStride) {
            case 0x00: return {sl.csr, BusStatus::Ok};
            case 0x04: return {sl.div, BusStatus::Ok};
            case 0x08: return {sl.ctr, BusStatus::Ok};
            case 0x0C: return {static_cast<std::uint32_t>(sl.cc_a) |
                               (static_cast<std::uint32_t>(sl.cc_b) << 16), BusStatus::Ok};
            case 0x10: return {sl.top, BusStatus::Ok};
            default:   return {0u, BusStatus::Ok};
        }
    }
    switch (offset) {
        case kEN:   return {enable_, BusStatus::Ok};
        case kINTR: return {intr_, BusStatus::Ok};
        case kINTE: return {inte_, BusStatus::Ok};
        case kINTF: return {intf_, BusStatus::Ok};
        case kINTS: return {(intr_ | intf_) & inte_, BusStatus::Ok};
        default:    return {0u, BusStatus::Ok};
    }
}

BusStatus Pwm::reg_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    if (offset < kNumSlices * kSliceStride) {
        const unsigned s = offset / kSliceStride;
        Slice& sl = slice_[s];
        switch (offset % kSliceStride) {
            case 0x00: {
                // PH_ADV/PH_RET are self-clearing strobes (datasheet
                // 4.5.2.1: "write a 1, and poll until low"), not stored
                // state - applied immediately (this simulator has no
                // hardware latency for them to poll through), then masked
                // out so a read-back always sees them already clear. Per
                // the datasheet, PH_RET is disabled while PH_ADV is set in
                // the same write.
                sl.csr = value & ~(kCSR_PH_ADV | kCSR_PH_RET);
                if (value & kCSR_PH_ADV) advance_slice(s);
                else if (value & kCSR_PH_RET) retard_slice(s);
                update_outputs(s);  // DIVMODE may have just changed B's OE
                break;
            }
            case 0x04: sl.div = value & 0xFFFu; break;
            case 0x08:
                sl.ctr = static_cast<std::uint16_t>(value);
                sl.counting_down = false;
                update_outputs(s);
                break;
            case 0x0C:
                sl.cc_a = static_cast<std::uint16_t>(value);
                sl.cc_b = static_cast<std::uint16_t>(value >> 16);
                update_outputs(s);
                break;
            case 0x10: sl.top = static_cast<std::uint16_t>(value); break;
            default: break;
        }
        return BusStatus::Ok;
    }
    switch (offset) {
        case kEN:   enable_ = value & 0xFFu; break;
        case kINTR: intr_ &= ~value; refresh_irq(); break;   // write-1-clear
        case kINTE: inte_ = value & 0xFFu; refresh_irq(); break;
        case kINTF: intf_ = value & 0xFFu; refresh_irq(); break;
        default: break;
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
