#include "peripherals/watchdog.h"

namespace rp2040 {

namespace {
enum : std::uint32_t {
    kCTRL = 0x00, kLOAD = 0x04, kREASON = 0x08,
    kSCRATCH0 = 0x0C,  // SCRATCH0..7 at +0,4,..,0x1C
    kTICK = 0x2C,
};
constexpr std::uint32_t kCTRL_ENABLE  = 1u << 30;
constexpr std::uint32_t kCTRL_TRIGGER = 1u << 31;
constexpr std::uint32_t kCTRL_TIME_MASK = 0x00FFFFFFu;

constexpr std::uint32_t kREASON_TIMER = 1u << 0;
constexpr std::uint32_t kREASON_FORCE = 1u << 1;

// PSM_WDSEL.PROC1 (datasheet 2.13): bit 16 of the PSM register block.
constexpr std::uint32_t kPsmWdselProc1 = 1u << 16;
}  // namespace

void Watchdog::fire_reset(std::uint32_t reason_bit) {
    reason_ = reason_bit;   // SCRATCH is preserved across the reset
    counter_ = 0;
    enabled_ = false;
    const std::uint32_t psm_wdsel = wdsel_provider_ ? wdsel_provider_() : 0u;
    if (reset_cb_) reset_cb_((psm_wdsel & kPsmWdselProc1) != 0);
    if (periph_reset_cb_) {
        periph_reset_cb_(resets_wdsel_provider_ ? resets_wdsel_provider_() : 0u);
    }
}

void Watchdog::on_cycles(std::uint64_t sys_cycles) {
    if (!enabled_) return;
    accum_ += sys_cycles;
    while (accum_ >= cycles_per_us_) {
        accum_ -= cycles_per_us_;
        // Documented quirk: the counter decrements by 2 per tick.
        if (counter_ <= 2u) {
            fire_reset(kREASON_TIMER);
            return;
        }
        counter_ -= 2u;
    }
}

BusResult<std::uint32_t> Watchdog::bus_read(std::uint32_t offset, BusWidth) {
    switch (offset) {
        case kCTRL:
            return {(ctrl_ & ~kCTRL_TIME_MASK) | (counter_ & kCTRL_TIME_MASK) |
                    (enabled_ ? kCTRL_ENABLE : 0u), BusStatus::Ok};
        case kLOAD:   return {load_, BusStatus::Ok};
        case kREASON: return {reason_, BusStatus::Ok};
        case kTICK:   return {tick_ | (1u << 10), BusStatus::Ok};  // RUNNING
        default:
            if (offset >= kSCRATCH0 && offset < kSCRATCH0 + 8 * 4) {
                return {scratch_[(offset - kSCRATCH0) / 4u], BusStatus::Ok};
            }
            return {0u, BusStatus::Ok};
    }
}

BusStatus Watchdog::bus_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    switch (offset) {
        case kCTRL:
            ctrl_ = value;
            enabled_ = (value & kCTRL_ENABLE) != 0;
            if ((value & kCTRL_TRIGGER) != 0) fire_reset(kREASON_FORCE);
            break;
        case kLOAD:
            load_ = value & kCTRL_TIME_MASK;
            counter_ = load_;   // feeding the watchdog
            break;
        case kREASON:
            break;  // read-only
        case kTICK:
            tick_ = value;
            break;
        default:
            if (offset >= kSCRATCH0 && offset < kSCRATCH0 + 8 * 4) {
                scratch_[(offset - kSCRATCH0) / 4u] = value;
            }
            break;
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
