#include "peripherals/timer.h"

namespace rp2040 {

namespace {
enum : std::uint32_t {
    kTIMEHW = 0x00, kTIMELW = 0x04, kTIMEHR = 0x08, kTIMELR = 0x0C,
    kALARM0 = 0x10,  // ALARM0..3 at +0,4,8,C
    kARMED = 0x20,
    kTIMERAWH = 0x24, kTIMERAWL = 0x28,
    kDBGPAUSE = 0x2C, kPAUSE = 0x30,
    kINTR = 0x34, kINTE = 0x38, kINTF = 0x3C, kINTS = 0x40,
};
}  // namespace

Timer::Timer(Cpu& cpu, std::uint32_t cycles_per_us)
    : nvic_(cpu), cycles_per_us_(cycles_per_us == 0 ? 1u : cycles_per_us) {}

void Timer::fire_due_alarms() {
    const std::uint32_t low = static_cast<std::uint32_t>(counter_ & 0xFFFFFFFFu);
    for (unsigned i = 0; i < kNumAlarms; ++i) {
        const std::uint8_t bit = static_cast<std::uint8_t>(1u << i);
        if ((armed_ & bit) != 0 && alarm_[i] == low) {
            armed_ = static_cast<std::uint8_t>(armed_ & ~bit);  // auto-disarm
            intr_ = static_cast<std::uint8_t>(intr_ | bit);
        }
    }
    refresh_irqs();
}

void Timer::refresh_irqs() {
    const std::uint8_t asserted = static_cast<std::uint8_t>((intr_ | intf_) & inte_);
    for (unsigned i = 0; i < kNumAlarms; ++i) {
        if ((asserted & (1u << i)) != 0) nvic_.pend_exception(kIrq0 + i);
        else                             nvic_.clear_pending(kIrq0 + i);
    }
}

void Timer::on_cycles(std::uint64_t sys_cycles) {
    if (paused_) return;
    accum_ += sys_cycles;
    while (accum_ >= cycles_per_us_) {
        accum_ -= cycles_per_us_;
        ++counter_;
        fire_due_alarms();
    }
}

void Timer::reset() {
    accum_ = 0;
    counter_ = 0;
    write_staging_hi_ = 0;
    read_latch_hi_ = 0;
    paused_ = false;
    alarm_.fill(0);
    armed_ = 0;
    intr_ = 0;
    inte_ = 0;
    intf_ = 0;
    refresh_irqs();
}

BusResult<std::uint32_t> Timer::reg_read(std::uint32_t offset, BusWidth) {
    switch (offset) {
        case kTIMELR:
            read_latch_hi_ = static_cast<std::uint32_t>(counter_ >> 32);
            return {static_cast<std::uint32_t>(counter_ & 0xFFFFFFFFu), BusStatus::Ok};
        case kTIMEHR:
            return {read_latch_hi_, BusStatus::Ok};
        case kTIMERAWL:
            return {static_cast<std::uint32_t>(counter_ & 0xFFFFFFFFu), BusStatus::Ok};
        case kTIMERAWH:
            return {static_cast<std::uint32_t>(counter_ >> 32), BusStatus::Ok};
        case kARMED:  return {armed_, BusStatus::Ok};
        case kPAUSE:  return {paused_ ? 1u : 0u, BusStatus::Ok};
        case kINTR:   return {intr_, BusStatus::Ok};
        case kINTE:   return {inte_, BusStatus::Ok};
        case kINTF:   return {intf_, BusStatus::Ok};
        case kINTS:   return {static_cast<std::uint32_t>((intr_ | intf_) & inte_), BusStatus::Ok};
        default:
            if (offset >= kALARM0 && offset < kALARM0 + kNumAlarms * 4u) {
                return {alarm_[(offset - kALARM0) / 4u], BusStatus::Ok};
            }
            return {0u, BusStatus::Ok};
    }
}

BusStatus Timer::reg_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    switch (offset) {
        case kTIMEHW:
            write_staging_hi_ = value;
            break;
        case kTIMELW:
            counter_ = (static_cast<std::uint64_t>(write_staging_hi_) << 32) | value;
            break;
        case kARMED:
            // write-1-to-disarm
            armed_ = static_cast<std::uint8_t>(armed_ & ~(value & 0xFu));
            refresh_irqs();
            break;
        case kPAUSE:
            paused_ = (value & 1u) != 0;
            break;
        case kINTR:
            intr_ = static_cast<std::uint8_t>(intr_ & ~(value & 0xFu));  // write-1-clear
            refresh_irqs();
            break;
        case kINTE:
            inte_ = static_cast<std::uint8_t>(value & 0xFu);
            refresh_irqs();
            break;
        case kINTF:
            intf_ = static_cast<std::uint8_t>(value & 0xFu);  // force
            refresh_irqs();
            break;
        default:
            if (offset >= kALARM0 && offset < kALARM0 + kNumAlarms * 4u) {
                const unsigned i = (offset - kALARM0) / 4u;
                alarm_[i] = value;
                armed_ = static_cast<std::uint8_t>(armed_ | (1u << i));  // writing arms it
                fire_due_alarms();  // an alarm set to "now" fires immediately
            }
            break;
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
