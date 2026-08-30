#include "peripherals/sysinfo.h"

namespace rp2040 {

namespace {
enum : std::uint32_t {
    kCHIP_ID = 0x00,
    kPLATFORM = 0x04,
    kGITREF_RP2040 = 0x40,
};
}  // namespace

BusResult<std::uint32_t> Sysinfo::bus_read(std::uint32_t offset, BusWidth) {
    switch (offset) {
        case kCHIP_ID:       return {kChipId, BusStatus::Ok};
        case kPLATFORM:      return {kPlatformAsic, BusStatus::Ok};
        case kGITREF_RP2040: return {kGitRef, BusStatus::Ok};
        default:             return {0u, BusStatus::Ok};
    }
}

BusStatus Sysinfo::bus_write(std::uint32_t, std::uint32_t, BusWidth) {
    return BusStatus::WriteToReadOnly;
}

}  // namespace rp2040
