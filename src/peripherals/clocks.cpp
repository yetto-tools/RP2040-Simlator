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

// --- ROSC ---------------------------------------------------------------
namespace {
enum : std::uint32_t {
    kROSC_CTRL = 0x00, kROSC_FREQA = 0x04, kROSC_FREQB = 0x08, kROSC_DORMANT = 0x0C,
    kROSC_DIV = 0x10, kROSC_PHASE = 0x14, kROSC_STATUS = 0x18, kROSC_RANDOMBIT = 0x1C,
    kROSC_COUNT = 0x20,
};
constexpr std::uint32_t kROSC_ENABLE_DISABLE = 0xD1Eu;   // CTRL.ENABLE = DISABLE
constexpr std::uint32_t kROSC_FREQ_PASSWD = 0x9696u;     // FREQA/FREQB bits [31:16]
constexpr std::uint32_t kROSC_STATUS_ENABLED = 1u << 12;
constexpr std::uint32_t kROSC_STATUS_DIV_RUNNING = 1u << 16;
constexpr std::uint32_t kROSC_STATUS_BADWRITE = 1u << 24;
constexpr std::uint32_t kROSC_STATUS_STABLE = 1u << 31;

bool rosc_enabled(std::uint32_t ctrl) {
    return ((ctrl >> 12) & 0xFFFu) != kROSC_ENABLE_DISABLE;
}
}  // namespace

BusResult<std::uint32_t> Rosc::reg_read(std::uint32_t reg, BusWidth) {
    switch (reg) {
        case kROSC_CTRL:    return {ctrl_, BusStatus::Ok};
        case kROSC_FREQA:   return {freqa_, BusStatus::Ok};
        case kROSC_FREQB:   return {freqb_, BusStatus::Ok};
        case kROSC_DORMANT: return {dormant_, BusStatus::Ok};
        case kROSC_DIV:     return {div_, BusStatus::Ok};
        case kROSC_PHASE:   return {phase_, BusStatus::Ok};
        case kROSC_STATUS: {
            std::uint32_t s = 0;
            if (rosc_enabled(ctrl_)) s |= kROSC_STATUS_ENABLED | kROSC_STATUS_DIV_RUNNING |
                                          kROSC_STATUS_STABLE;
            if (badwrite_) s |= kROSC_STATUS_BADWRITE;
            return {s, BusStatus::Ok};
        }
        case kROSC_RANDOMBIT: {
            const std::uint32_t v = randbit_ ? 1u : 0u;
            randbit_ = !randbit_;
            return {v, BusStatus::Ok};
        }
        case kROSC_COUNT:   return {0u, BusStatus::Ok};
        default:            return {0u, BusStatus::Ok};
    }
}

BusStatus Rosc::reg_write(std::uint32_t reg, std::uint32_t value, BusWidth) {
    switch (reg) {
        case kROSC_CTRL:    ctrl_ = value; break;
        case kROSC_FREQA:
            if (((value >> 16) & 0xFFFFu) != kROSC_FREQ_PASSWD) { badwrite_ = true; break; }
            freqa_ = value & 0xFFFFu;
            break;
        case kROSC_FREQB:
            if (((value >> 16) & 0xFFFFu) != kROSC_FREQ_PASSWD) { badwrite_ = true; break; }
            freqb_ = value & 0xFFFFu;
            break;
        case kROSC_DORMANT: dormant_ = value; break;
        case kROSC_DIV:     div_ = value & 0xFFFu; break;
        case kROSC_PHASE:   phase_ = value & 0xFFFu; break;
        case kROSC_STATUS:
            if (value & kROSC_STATUS_BADWRITE) badwrite_ = false;  // write-1-to-clear
            break;
        default: break;
    }
    return BusStatus::Ok;
}

// --- PLL ----------------------------------------------------------------
namespace {
enum : std::uint32_t { kPLL_CS = 0x00, kPLL_PWR = 0x04, kPLL_FBDIV_INT = 0x08, kPLL_PRIM = 0x0C };
constexpr std::uint32_t kPLL_CS_LOCK = 1u << 31;
constexpr std::uint32_t kPLL_CS_BYPASS = 1u << 8;
}  // namespace

void Pll::reset() {
    cs_ = 0;
    pwr_ = 0xFFFFFFFFu;
    fbdiv_ = 0;
    prim_ = 0x00070700u;
}

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

bool Pll::locked() const {
    return (cs_ & kPLL_CS_BYPASS) == 0 && (pwr_ & 0x1u) == 0;
}

unsigned Pll::postdiv1() const { return (prim_ >> 16) & 0x7u; }
unsigned Pll::postdiv2() const { return (prim_ >> 12) & 0x7u; }
unsigned Pll::feedback_divider() const { return fbdiv_ & 0xFFFu; }

std::uint64_t Pll::output_hz(std::uint64_t ref_hz) const {
    const unsigned fb = feedback_divider();
    const unsigned pd1 = postdiv1();
    const unsigned pd2 = postdiv2();
    if (!locked() || fb == 0 || pd1 == 0 || pd2 == 0) return 0;
    return ref_hz * fb / (static_cast<std::uint64_t>(pd1) * pd2);
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
            case 0x00: gen_[g].ctrl = value; configured_ |= (1u << g); break;
            case 0x04: gen_[g].div = value; configured_ |= (1u << g); break;
            default: break;  // SELECTED is read-only
        }
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
