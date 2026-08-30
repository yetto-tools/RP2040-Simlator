#include "peripherals/rtc.h"

namespace rp2040 {

namespace {
enum : std::uint32_t {
    kCLKDIV_M1 = 0x00, kSETUP_0 = 0x04, kSETUP_1 = 0x08, kCTRL = 0x0C,
    kIRQ_SETUP_0 = 0x10, kIRQ_SETUP_1 = 0x14, kRTC_1 = 0x18, kRTC_0 = 0x1C,
    kINTR = 0x20, kINTE = 0x24, kINTF = 0x28, kINTS = 0x2C,
};
constexpr std::uint32_t kCTRL_ENABLE = 1u << 0;
constexpr std::uint32_t kCTRL_LOAD   = 1u << 4;

// Alarm MATCH-enable bits inside IRQ_SETUP_0 / IRQ_SETUP_1.
constexpr std::uint32_t kMATCH_YEAR  = 1u << 26;   // IRQ_SETUP_0
constexpr std::uint32_t kMATCH_MONTH = 1u << 25;
constexpr std::uint32_t kMATCH_DAY   = 1u << 24;
constexpr std::uint32_t kMATCH_DOTW  = 1u << 31;   // IRQ_SETUP_1
constexpr std::uint32_t kMATCH_HOUR  = 1u << 30;
constexpr std::uint32_t kMATCH_MIN   = 1u << 29;
constexpr std::uint32_t kMATCH_SEC   = 1u << 28;

unsigned days_in_month(unsigned year, unsigned month) {
    static const unsigned d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) return 29;
    return d[(month - 1) % 12];
}
}  // namespace

std::uint32_t Rtc::pack_date(const DateTime& t) const {
    return ((t.year & 0xFFFu) << 12) | ((t.month & 0xFu) << 8) | (t.day & 0x1Fu);
}
std::uint32_t Rtc::pack_time(const DateTime& t) const {
    return ((t.dotw & 0x7u) << 24) | ((t.hour & 0x1Fu) << 16) |
           ((t.min & 0x3Fu) << 8) | (t.sec & 0x3Fu);
}

void Rtc::tick_second() {
    if (++now_.sec < 60) { check_alarm(); return; }
    now_.sec = 0;
    if (++now_.min < 60) { check_alarm(); return; }
    now_.min = 0;
    if (++now_.hour < 24) { check_alarm(); return; }
    now_.hour = 0;
    now_.dotw = (now_.dotw + 1) % 7;
    if (++now_.day <= days_in_month(now_.year, now_.month)) { check_alarm(); return; }
    now_.day = 1;
    if (++now_.month <= 12) { check_alarm(); return; }
    now_.month = 1;
    ++now_.year;
    check_alarm();
}

void Rtc::check_alarm() {
    const bool year_ok  = (irq_setup0_ & kMATCH_YEAR)  == 0 || ((irq_setup0_ >> 12) & 0xFFFu) == now_.year;
    const bool month_ok = (irq_setup0_ & kMATCH_MONTH) == 0 || ((irq_setup0_ >> 8) & 0xFu) == now_.month;
    const bool day_ok   = (irq_setup0_ & kMATCH_DAY)   == 0 || (irq_setup0_ & 0x1Fu) == now_.day;
    const bool dotw_ok  = (irq_setup1_ & kMATCH_DOTW)  == 0 || ((irq_setup1_ >> 24) & 0x7u) == now_.dotw;
    const bool hour_ok  = (irq_setup1_ & kMATCH_HOUR)  == 0 || ((irq_setup1_ >> 16) & 0x1Fu) == now_.hour;
    const bool min_ok   = (irq_setup1_ & kMATCH_MIN)   == 0 || ((irq_setup1_ >> 8) & 0x3Fu) == now_.min;
    const bool sec_ok   = (irq_setup1_ & kMATCH_SEC)   == 0 || (irq_setup1_ & 0x3Fu) == now_.sec;

    // At least one field must be match-enabled for the alarm to be armed.
    const bool armed = (irq_setup0_ & (kMATCH_YEAR | kMATCH_MONTH | kMATCH_DAY)) != 0 ||
                       (irq_setup1_ & (kMATCH_DOTW | kMATCH_HOUR | kMATCH_MIN | kMATCH_SEC)) != 0;

    const bool matched = armed && year_ok && month_ok && day_ok &&
                         dotw_ok && hour_ok && min_ok && sec_ok;
    if (matched && !alarm_matched_) intr_ |= 1u;  // rising-edge only
    alarm_matched_ = matched;

    const std::uint32_t mis = (intr_ | intf_) & inte_;
    if (mis != 0) nvic_.pend_exception(kIrq);
    else          nvic_.clear_pending(kIrq);
}

void Rtc::on_cycles(std::uint64_t sys_cycles) {
    if (!enabled_) return;
    sub_accum_ += sys_cycles * rtc_hz_;
    while (sub_accum_ >= sys_hz_) {
        sub_accum_ -= sys_hz_;
        if (++div_counter_ > clkdiv_m1_) {
            div_counter_ = 0;
            tick_second();
        }
    }
}

BusResult<std::uint32_t> Rtc::reg_read(std::uint32_t reg, BusWidth) {
    switch (reg) {
        case kCLKDIV_M1:   return {clkdiv_m1_, BusStatus::Ok};
        case kSETUP_0:     return {setup0_, BusStatus::Ok};
        case kSETUP_1:     return {setup1_, BusStatus::Ok};
        case kCTRL:        return {(enabled_ ? (kCTRL_ENABLE | (1u << 1)) : 0u), BusStatus::Ok};
        case kIRQ_SETUP_0: return {irq_setup0_, BusStatus::Ok};
        case kIRQ_SETUP_1: return {irq_setup1_, BusStatus::Ok};
        case kRTC_1:       return {pack_date(now_), BusStatus::Ok};
        case kRTC_0:       return {pack_time(now_), BusStatus::Ok};
        case kINTR:        return {intr_, BusStatus::Ok};
        case kINTE:        return {inte_, BusStatus::Ok};
        case kINTF:        return {intf_, BusStatus::Ok};
        case kINTS:        return {(intr_ | intf_) & inte_, BusStatus::Ok};
        default:           return {0u, BusStatus::Ok};
    }
}

BusStatus Rtc::reg_write(std::uint32_t reg, std::uint32_t value, BusWidth) {
    switch (reg) {
        case kCLKDIV_M1: clkdiv_m1_ = value; break;
        case kSETUP_0:   setup0_ = value; break;
        case kSETUP_1:   setup1_ = value; break;
        case kCTRL:
            enabled_ = (value & kCTRL_ENABLE) != 0;
            if ((value & kCTRL_LOAD) != 0) {
                now_.year  = (setup0_ >> 12) & 0xFFFu;
                now_.month = (setup0_ >> 8) & 0xFu;
                now_.day   = setup0_ & 0x1Fu;
                now_.dotw  = (setup1_ >> 24) & 0x7u;
                now_.hour  = (setup1_ >> 16) & 0x1Fu;
                now_.min   = (setup1_ >> 8) & 0x3Fu;
                now_.sec   = setup1_ & 0x3Fu;
                div_counter_ = 0;
            }
            break;
        case kIRQ_SETUP_0: irq_setup0_ = value; intr_ = 0; break;  // (re)arming clears
        case kIRQ_SETUP_1: irq_setup1_ = value; intr_ = 0; break;
        case kINTR: intr_ &= ~value; break;   // write-1-clear (SDK convenience)
        case kINTE: inte_ = value & 1u; break;
        case kINTF: intf_ = value & 1u; break;
        default: break;
    }
    check_alarm();
    return BusStatus::Ok;
}

}  // namespace rp2040
