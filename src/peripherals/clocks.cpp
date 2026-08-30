#include "peripherals/clocks.h"

namespace rp2040 {

// --- XOSC -----------------------------------------------------------------
namespace {
enum : std::uint32_t { kXOSC_CTRL = 0x00, kXOSC_STATUS = 0x04, kXOSC_STARTUP = 0x0C, kXOSC_COUNT = 0x1C };
constexpr std::uint32_t kXOSC_ENABLE_MAGIC = 0xFAB000u;   // CTRL.ENABLE = 0xFAB
constexpr std::uint32_t kXOSC_STATUS_STABLE = 1u << 31;
constexpr std::uint32_t kXOSC_STATUS_ENABLED = 1u << 12;
}  // namespace

BusResult<std::uint32_t> Xosc::reg_read(std::uint32_t reg, BusWidth) {
    switch (reg) {
        case kXOSC_CTRL: return {ctrl_, BusStatus::Ok};
        case kXOSC_STATUS: {
            const bool en = (ctrl_ & 0xFFF000u) == kXOSC_ENABLE_MAGIC;
            std::uint32_t s = 0;
            if (en) s |= kXOSC_STATUS_STABLE | kXOSC_STATUS_ENABLED;
            return {s, BusStatus::Ok};
        }
        case kXOSC_STARTUP: return {startup_, BusStatus::Ok};
        case kXOSC_COUNT:   return {0u, BusStatus::Ok};
        default:            return {0u, BusStatus::Ok};
    }
}

BusStatus Xosc::reg_write(std::uint32_t reg, std::uint32_t value, BusWidth) {
    if (reg == kXOSC_CTRL) ctrl_ = value;
    else if (reg == kXOSC_STARTUP) startup_ = value;
    return BusStatus::Ok;
}

// --- PLL ----------------------------------------------------------------
namespace {
enum : std::uint32_t { kPLL_CS = 0x00, kPLL_PWR = 0x04, kPLL_FBDIV_INT = 0x08, kPLL_PRIM = 0x0C };
constexpr std::uint32_t kPLL_CS_LOCK = 1u << 31;
constexpr std::uint32_t kPLL_CS_BYPASS = 1u << 8;
}  // namespace

BusResult<std::uint32_t> Pll::reg_read(std::uint32_t reg, BusWidth) {
    switch (reg) {
        case kPLL_CS: {
            std::uint32_t cs = cs_ & ~kPLL_CS_LOCK;
            if ((cs_ & kPLL_CS_BYPASS) == 0 && (pwr_ & 0x1u) == 0) cs |= kPLL_CS_LOCK;
            return {cs, BusStatus::Ok};
        }
        case kPLL_PWR:       return {pwr_, BusStatus::Ok};
        case kPLL_FBDIV_INT: return {fbdiv_, BusStatus::Ok};
        case kPLL_PRIM:      return {prim_, BusStatus::Ok};
        default:            return {0u, BusStatus::Ok};
    }
}

BusStatus Pll::reg_write(std::uint32_t reg, std::uint32_t value, BusWidth) {
    switch (reg) {
        case kPLL_CS:        cs_ = value; break;
        case kPLL_PWR:       pwr_ = value; break;
        case kPLL_FBDIV_INT: fbdiv_ = value & 0xFFFu; break;
        case kPLL_PRIM:      prim_ = value; break;
        default: break;
    }
    return BusStatus::Ok;
}

// --- CLOCKS ------------------------------------------------------------
// Each generator occupies 0x0C bytes: CTRL, DIV, SELECTED.
namespace { constexpr std::uint32_t kGenStride = 0x0C; }

BusResult<std::uint32_t> Clocks::reg_read(std::uint32_t reg, BusWidth) {
    if (reg < kNumGenerators * kGenStride) {
        const unsigned g = reg / kGenStride;
        switch (reg % kGenStride) {
            case 0x00: return {gen_[g].ctrl, BusStatus::Ok};
            case 0x04: return {gen_[g].div, BusStatus::Ok};
            case 0x08: {
                // SELECTED is a one-hot of the glitchless mux (CTRL.SRC, [1:0]);
                // for aux-only generators SRC is 0 and SELECTED reads 1.
                const std::uint32_t src = gen_[g].ctrl & 0x3u;
                return {1u << src, BusStatus::Ok};
            }
            default: return {0u, BusStatus::Ok};
        }
    }
    return {0u, BusStatus::Ok};  // ENABLED / INTR / RESUS / FC0 / ... read 0
}

BusStatus Clocks::reg_write(std::uint32_t reg, std::uint32_t value, BusWidth) {
    if (reg < kNumGenerators * kGenStride) {
        const unsigned g = reg / kGenStride;
        switch (reg % kGenStride) {
            case 0x00: gen_[g].ctrl = value; break;
            case 0x04: gen_[g].div = value; break;
            default: break;  // SELECTED is read-only
        }
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
