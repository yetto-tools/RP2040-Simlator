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
}  // namespace

void Pwm::update_outputs(unsigned s) {
    const Slice& sl = slice_[s];
    const bool a_hi = (sl.ctr < sl.cc_a) ^ ((sl.csr & kCSR_A_INV) != 0);
    const bool b_hi = (sl.ctr < sl.cc_b) ^ ((sl.csr & kCSR_B_INV) != 0);
    const unsigned pin_a = 2u * s;
    const unsigned pin_b = 2u * s + 1u;
    gpio_.driver_set_pin(Gpio::kPwm, pin_a, a_hi);
    gpio_.driver_set_pin(Gpio::kPwm, pin_a + 16u, a_hi);
    gpio_.driver_set_pin(Gpio::kPwm, pin_b, b_hi);
    if (pin_b + 16u < static_cast<unsigned>(Gpio::kNumPins))
        gpio_.driver_set_pin(Gpio::kPwm, pin_b + 16u, b_hi);
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
    if (mis != 0) cpu_.pend_exception(kIrqWrap);
    else          cpu_.clear_pending(kIrqWrap);
}

void Pwm::on_cycles(std::uint64_t sys_cycles) {
    for (std::uint64_t i = 0; i < sys_cycles; ++i) {
        for (unsigned s = 0; s < kNumSlices; ++s) {
            const Slice& sl = slice_[s];
            if ((enable_ & (1u << s)) == 0 && (sl.csr & kCSR_EN) == 0) continue;
            // Pace by the integer divider: one counter step every div_int clocks.
            slice_[s].frac_accum += 16u + (sl.div & 0xFu);
            const unsigned div_int = (sl.div >> 4) & 0xFFu;
            const unsigned period = (div_int == 0 ? 1u : div_int) * 16u;
            if (slice_[s].frac_accum >= period) {
                slice_[s].frac_accum -= period;
                advance_slice(s);
            }
        }
    }
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
            case 0x00: sl.csr = value; break;
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
