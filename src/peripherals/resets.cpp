#include "peripherals/resets.h"

namespace rp2040 {

namespace {
enum : std::uint32_t { kRESET = 0x00, kWDSEL = 0x04, kRESET_DONE = 0x08 };
constexpr std::uint32_t kAllPeriphs = 0x01FFFFFFu;
}  // namespace

BusResult<std::uint32_t> Resets::reg_read(std::uint32_t reg, BusWidth) {
    switch (reg) {
        case kRESET:      return {reset_, BusStatus::Ok};
        case kWDSEL:      return {wdsel_, BusStatus::Ok};
        case kRESET_DONE: return {~reset_ & kAllPeriphs, BusStatus::Ok};
        default:          return {0u, BusStatus::Ok};
    }
}

BusStatus Resets::reg_write(std::uint32_t reg, std::uint32_t value, BusWidth) {
    switch (reg) {
        case kRESET: reset_ = value & kAllPeriphs; break;
        case kWDSEL: wdsel_ = value & kAllPeriphs; break;
        default: break;
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
