#include "peripherals/resets.h"

namespace rp2040 {

namespace {
// The RESETS block exposes RESET / WDSEL / RESET_DONE plus the +0x1000/2000/
// 3000 atomic-alias windows (XOR / set / clear). Handle the aliases here.
enum : std::uint32_t { kRESET = 0x00, kWDSEL = 0x04, kRESET_DONE = 0x08 };

std::uint32_t apply_alias(std::uint32_t current, std::uint32_t value, std::uint32_t alias) {
    switch (alias) {
        case 0x1000: return current ^ value;   // XOR
        case 0x2000: return current | value;   // set
        case 0x3000: return current & ~value;  // clear
        default:     return value;             // normal write
    }
}
}  // namespace

BusResult<std::uint32_t> Resets::bus_read(std::uint32_t offset, BusWidth) {
    switch (offset & 0x0FFFu) {
        case kRESET:      return {reset_, BusStatus::Ok};
        case kWDSEL:      return {wdsel_, BusStatus::Ok};
        case kRESET_DONE: return {~reset_ & 0x01FFFFFFu, BusStatus::Ok};
        default:          return {0u, BusStatus::Ok};
    }
}

BusStatus Resets::bus_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    const std::uint32_t alias = offset & 0x3000u;
    switch (offset & 0x0FFFu) {
        case kRESET: reset_ = apply_alias(reset_, value, alias) & 0x01FFFFFFu; break;
        case kWDSEL: wdsel_ = apply_alias(wdsel_, value, alias) & 0x01FFFFFFu; break;
        default: break;
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
