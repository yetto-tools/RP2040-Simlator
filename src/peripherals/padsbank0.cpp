#include "peripherals/padsbank0.h"

namespace rp2040 {

namespace {
constexpr std::uint32_t kPadPue = 1u << 3;
constexpr std::uint32_t kPadPde = 1u << 2;
constexpr std::uint32_t kGpio0 = 0x04;  // GPIO0..29 at +0x04, +0x08, ...
}  // namespace

BusResult<std::uint32_t> PadsBank0::reg_read(std::uint32_t reg, BusWidth) {
    if (reg == 0x00) return {voltage_select_, BusStatus::Ok};
    if (reg >= kGpio0 && reg < kGpio0 + Gpio::kNumPins * 4u) {
        return {pad_[(reg - kGpio0) / 4u], BusStatus::Ok};
    }
    return {0u, BusStatus::Ok};
}

BusStatus PadsBank0::reg_write(std::uint32_t reg, std::uint32_t value, BusWidth) {
    if (reg == 0x00) {
        voltage_select_ = value;
        return BusStatus::Ok;
    }
    if (reg >= kGpio0 && reg < kGpio0 + Gpio::kNumPins * 4u) {
        const unsigned pin = (reg - kGpio0) / 4u;
        pad_[pin] = value & 0xFFu;
        gpio_.set_pulls(pin, (value & kPadPue) != 0, (value & kPadPde) != 0);
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
