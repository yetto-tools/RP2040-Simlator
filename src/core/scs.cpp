#include "core/scs.h"

#include "exceptions.h"

namespace rp2040 {

namespace {

// Register offsets from kBase (0xE000E000).
enum : std::uint32_t {
    kSYST_CSR   = 0x010, kSYST_RVR = 0x014, kSYST_CVR = 0x018, kSYST_CALIB = 0x01C,
    kNVIC_ISER  = 0x100, kNVIC_ICER = 0x180,
    kNVIC_ISPR  = 0x200, kNVIC_ICPR = 0x280,
    kNVIC_IPR0  = 0x400,  // .. 0x41C (IPR0..IPR7)
    kCPUID      = 0xD00, kICSR = 0xD04, kVTOR = 0xD08, kAIRCR = 0xD0C,
    kSCR        = 0xD10, kCCR  = 0xD14,
    kSHPR2      = 0xD1C, kSHPR3 = 0xD20, kSHCSR = 0xD24,
};

constexpr std::uint32_t kAircrVectKey = 0x05FAu << 16;
constexpr std::uint32_t kSysTick24    = 0x00FFFFFFu;
constexpr std::uint32_t kCountFlag    = 1u << 16;

}  // namespace

std::uint32_t Scs::read_ipr(unsigned word) const {
    std::uint32_t v = 0;
    for (unsigned b = 0; b < 4; ++b) {
        const unsigned irq = word * 4 + b;
        v |= static_cast<std::uint32_t>(cpu_.exception_priority(kExcExternal0 + irq)) << (b * 8);
    }
    return v;
}

void Scs::write_ipr(unsigned word, std::uint32_t value) {
    for (unsigned b = 0; b < 4; ++b) {
        const unsigned irq = word * 4 + b;
        if (irq >= static_cast<unsigned>(kNumRp2040Irqs)) break;
        cpu_.set_exception_priority(kExcExternal0 + irq,
                                   static_cast<std::uint8_t>((value >> (b * 8)) & 0xFFu));
    }
}

std::uint32_t Scs::read_shpr2() const {
    return static_cast<std::uint32_t>(cpu_.exception_priority(kExcSVCall)) << 24;
}

std::uint32_t Scs::read_shpr3() const {
    return (static_cast<std::uint32_t>(cpu_.exception_priority(kExcSysTick)) << 24) |
           (static_cast<std::uint32_t>(cpu_.exception_priority(kExcPendSV)) << 16);
}

BusResult<std::uint32_t> Scs::bus_read(std::uint32_t offset, BusWidth) {
    switch (offset) {
        case kSYST_CSR: {
            const std::uint32_t v = syst_csr_;
            syst_csr_ &= ~kCountFlag;  // COUNTFLAG clears on read
            return {v, BusStatus::Ok};
        }
        case kSYST_RVR:  return {syst_rvr_ & kSysTick24, BusStatus::Ok};
        case kSYST_CVR:  return {syst_cvr_ & kSysTick24, BusStatus::Ok};
        case kSYST_CALIB: return {0u, BusStatus::Ok};  // no calibration data

        case kNVIC_ISER:
        case kNVIC_ICER: {
            std::uint32_t v = 0;
            for (unsigned i = 0; i < static_cast<unsigned>(kNumRp2040Irqs); ++i) {
                if (cpu_.irq_enabled(i)) v |= (1u << i);
            }
            return {v, BusStatus::Ok};
        }
        case kNVIC_ISPR:
        case kNVIC_ICPR: {
            std::uint32_t v = 0;
            for (unsigned i = 0; i < static_cast<unsigned>(kNumRp2040Irqs); ++i) {
                if (cpu_.is_pending(kExcExternal0 + i)) v |= (1u << i);
            }
            return {v, BusStatus::Ok};
        }

        case kCPUID: return {kCpuid, BusStatus::Ok};
        case kICSR: {
            std::uint32_t v = cpu_.current_exception();  // VECTACTIVE
            if (cpu_.is_pending(kExcPendSV))  v |= (1u << 28);
            if (cpu_.is_pending(kExcSysTick)) v |= (1u << 26);
            return {v, BusStatus::Ok};
        }
        case kVTOR:  return {cpu_.vtor(), BusStatus::Ok};
        case kAIRCR: return {kAircrVectKey, BusStatus::Ok};  // key reads back
        case kSCR:   return {scr_, BusStatus::Ok};
        case kCCR:   return {(1u << 3) | (1u << 9), BusStatus::Ok};  // UNALIGN_TRP|STKALIGN
        case kSHPR2: return {read_shpr2(), BusStatus::Ok};
        case kSHPR3: return {read_shpr3(), BusStatus::Ok};
        case kSHCSR: return {0u, BusStatus::Ok};

        default:
            if (offset >= kNVIC_IPR0 && offset < kNVIC_IPR0 + 8 * 4) {
                return {read_ipr((offset - kNVIC_IPR0) / 4), BusStatus::Ok};
            }
            return {0u, BusStatus::Ok};  // reserved: RAZ
    }
}

BusStatus Scs::bus_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    switch (offset) {
        case kSYST_CSR:
            syst_csr_ = (syst_csr_ & kCountFlag) | (value & 0x7u);  // keep COUNTFLAG
            return BusStatus::Ok;
        case kSYST_RVR:
            syst_rvr_ = value & kSysTick24;
            return BusStatus::Ok;
        case kSYST_CVR:
            syst_cvr_ = 0;              // any write clears the counter ...
            syst_csr_ &= ~kCountFlag;   // ... and COUNTFLAG
            return BusStatus::Ok;

        case kNVIC_ISER:
            for (unsigned i = 0; i < 32; ++i)
                if (value & (1u << i)) cpu_.set_irq_enabled(i, true);
            return BusStatus::Ok;
        case kNVIC_ICER:
            for (unsigned i = 0; i < 32; ++i)
                if (value & (1u << i)) cpu_.set_irq_enabled(i, false);
            return BusStatus::Ok;
        case kNVIC_ISPR:
            for (unsigned i = 0; i < static_cast<unsigned>(kNumRp2040Irqs); ++i)
                if (value & (1u << i)) cpu_.pend_exception(kExcExternal0 + i);
            return BusStatus::Ok;
        case kNVIC_ICPR:
            for (unsigned i = 0; i < static_cast<unsigned>(kNumRp2040Irqs); ++i)
                if (value & (1u << i)) cpu_.clear_pending(kExcExternal0 + i);
            return BusStatus::Ok;

        case kICSR:
            if (value & (1u << 28)) cpu_.pend_exception(kExcPendSV);
            if (value & (1u << 27)) cpu_.clear_pending(kExcPendSV);
            if (value & (1u << 26)) cpu_.pend_exception(kExcSysTick);
            if (value & (1u << 25)) cpu_.clear_pending(kExcSysTick);
            return BusStatus::Ok;
        case kVTOR:
            cpu_.set_vtor(value);
            return BusStatus::Ok;
        case kAIRCR:
            if ((value & 0xFFFF0000u) != kAircrVectKey) return BusStatus::Ok;  // bad key
            if ((value & (1u << 2)) != 0) {                // SYSRESETREQ
                if (system_reset_cb_) system_reset_cb_();
                else cpu_.reset();
            }
            return BusStatus::Ok;
        case kSCR:
            scr_ = value & 0x16u;
            return BusStatus::Ok;
        case kSHPR2:
            cpu_.set_exception_priority(kExcSVCall, static_cast<std::uint8_t>(value >> 24));
            return BusStatus::Ok;
        case kSHPR3:
            cpu_.set_exception_priority(kExcSysTick, static_cast<std::uint8_t>(value >> 24));
            cpu_.set_exception_priority(kExcPendSV, static_cast<std::uint8_t>((value >> 16) & 0xFFu));
            return BusStatus::Ok;

        case kCPUID: case kCCR: case kSHCSR:
            return BusStatus::Ok;  // read-only / ignored

        default:
            if (offset >= kNVIC_IPR0 && offset < kNVIC_IPR0 + 8 * 4) {
                write_ipr((offset - kNVIC_IPR0) / 4, value);
            }
            return BusStatus::Ok;
    }
}

void Scs::on_cycles(std::uint64_t cycles) {
    if ((syst_csr_ & 1u) == 0) return;  // SysTick disabled

    const std::uint32_t reload = syst_rvr_ & kSysTick24;
    for (std::uint64_t i = 0; i < cycles; ++i) {
        if ((syst_cvr_ & kSysTick24) == 0) {
            syst_cvr_ = reload;  // reload happens the cycle after reaching 0
            continue;
        }
        syst_cvr_ = (syst_cvr_ - 1) & kSysTick24;
        if ((syst_cvr_ & kSysTick24) == 0) {
            syst_csr_ |= kCountFlag;
            if (syst_csr_ & (1u << 1)) {  // TICKINT
                cpu_.pend_exception(kExcSysTick);
            }
        }
    }
}

}  // namespace rp2040
