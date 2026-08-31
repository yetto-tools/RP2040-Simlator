#include "peripherals/iobank0.h"

namespace rp2040 {

namespace {
// Interrupt register banks (from kBase).
enum : std::uint32_t {
    kINTR0      = 0x0F0,   // 0x0F0..0x0FC
    kPROC0_INTE = 0x100,   // 0x100..0x10C
    kPROC0_INTF = 0x110,
    kPROC0_INTS = 0x120,
    kPROC1_INTE = 0x130,
    kPROC1_INTF = 0x140,
    kPROC1_INTS = 0x150,
};

// Per-pin interrupt bits within a group nibble.
constexpr unsigned kLEVEL_LOW  = 0;
constexpr unsigned kLEVEL_HIGH = 1;
constexpr unsigned kEDGE_LOW   = 2;
constexpr unsigned kEDGE_HIGH  = 3;
constexpr std::uint32_t kEDGE_MASK_NIBBLE = (1u << kEDGE_LOW) | (1u << kEDGE_HIGH);

std::uint32_t all_edge_mask() {
    std::uint32_t m = 0;
    for (unsigned i = 0; i < 8; ++i) m |= kEDGE_MASK_NIBBLE << (i * 4u);
    return m;
}
}  // namespace

void IoBank0::poll() {
    std::uint32_t level = 0;
    for (unsigned p = 0; p < Gpio::kNumPins; ++p) {
        if (gpio_.irq_level(p)) level |= (1u << p);   // IRQOVER applied
    }
    const std::uint32_t rising  = primed_ ? (level & ~prev_level_) : 0u;
    const std::uint32_t falling = primed_ ? (~level & prev_level_) : 0u;
    prev_level_ = level;
    primed_ = true;

    for (unsigned g = 0; g < kIntGroups; ++g) {
        std::uint32_t v = intr_[g] & all_edge_mask();  // keep sticky edge bits
        for (unsigned k = 0; k < 8; ++k) {
            const unsigned pin = g * 8u + k;
            if (pin >= Gpio::kNumPins) break;
            const unsigned base = k * 4u;
            const bool hi = (level >> pin) & 1u;
            if (hi) v |= (1u << (base + kLEVEL_HIGH));
            else    v |= (1u << (base + kLEVEL_LOW));
            if ((rising  >> pin) & 1u) v |= (1u << (base + kEDGE_HIGH));
            if ((falling >> pin) & 1u) v |= (1u << (base + kEDGE_LOW));
        }
        intr_[g] = v;
    }
    refresh_irq();
}

void IoBank0::refresh_irq() {
    for (unsigned c = 0; c < 2; ++c) {
        if (core_[c] == nullptr) continue;
        std::uint32_t any = 0;
        for (unsigned g = 0; g < kIntGroups; ++g) {
            any |= (intr_[g] | intf_[c][g]) & inte_[c][g];
        }
        if (any != 0) core_[c]->pend_exception(kIrqBank0);
        else          core_[c]->clear_pending(kIrqBank0);
    }
}

BusResult<std::uint32_t> IoBank0::reg_read(std::uint32_t offset, BusWidth) {
    // Each GPIO occupies 8 bytes: +0 = GPIOx_STATUS, +4 = GPIOx_CTRL.
    if (offset < Gpio::kNumPins * 8u) {
        const unsigned pin = offset / 8u;
        if ((offset & 4u) != 0) return {ctrl_[pin], BusStatus::Ok};        // CTRL
        std::uint32_t status = 0;
        if (gpio_.pad_level(pin)) status |= (1u << 9);
        if (gpio_.level(pin))     status |= (1u << 17);
        return {status, BusStatus::Ok};
    }

    if (offset >= kINTR0 && offset < kINTR0 + kIntGroups * 4u) {
        return {intr_[(offset - kINTR0) / 4u], BusStatus::Ok};
    }
    auto group_read = [&](std::uint32_t base, const std::array<std::uint32_t, kIntGroups>& arr) {
        return arr[(offset - base) / 4u];
    };
    if (offset >= kPROC0_INTE && offset < kPROC0_INTE + kIntGroups * 4u)
        return {group_read(kPROC0_INTE, inte_[0]), BusStatus::Ok};
    if (offset >= kPROC0_INTF && offset < kPROC0_INTF + kIntGroups * 4u)
        return {group_read(kPROC0_INTF, intf_[0]), BusStatus::Ok};
    if (offset >= kPROC1_INTE && offset < kPROC1_INTE + kIntGroups * 4u)
        return {group_read(kPROC1_INTE, inte_[1]), BusStatus::Ok};
    if (offset >= kPROC1_INTF && offset < kPROC1_INTF + kIntGroups * 4u)
        return {group_read(kPROC1_INTF, intf_[1]), BusStatus::Ok};
    if (offset >= kPROC0_INTS && offset < kPROC0_INTS + kIntGroups * 4u) {
        const unsigned g = (offset - kPROC0_INTS) / 4u;
        return {(intr_[g] | intf_[0][g]) & inte_[0][g], BusStatus::Ok};
    }
    if (offset >= kPROC1_INTS && offset < kPROC1_INTS + kIntGroups * 4u) {
        const unsigned g = (offset - kPROC1_INTS) / 4u;
        return {(intr_[g] | intf_[1][g]) & inte_[1][g], BusStatus::Ok};
    }
    return {0u, BusStatus::Ok};
}

BusStatus IoBank0::reg_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    if (offset < Gpio::kNumPins * 8u && (offset & 4u) != 0) {
        const unsigned pin = offset / 8u;
        ctrl_[pin] = value;
        gpio_.set_funcsel(pin, static_cast<std::uint8_t>(value & 0x1Fu));
        gpio_.set_overrides(pin,
                            static_cast<std::uint8_t>((value >> 8) & 0x3u),    // OUTOVER
                            static_cast<std::uint8_t>((value >> 12) & 0x3u),   // OEOVER
                            static_cast<std::uint8_t>((value >> 16) & 0x3u),   // INOVER
                            static_cast<std::uint8_t>((value >> 30) & 0x3u));  // IRQOVER
        return BusStatus::Ok;
    }

    if (offset >= kINTR0 && offset < kINTR0 + kIntGroups * 4u) {
        // Only the edge bits are write-1-to-clear; level bits are read-only.
        intr_[(offset - kINTR0) / 4u] &= ~(value & all_edge_mask());
        refresh_irq();
        return BusStatus::Ok;
    }
    auto group_write = [&](std::uint32_t base, std::array<std::uint32_t, kIntGroups>& arr) {
        arr[(offset - base) / 4u] = value;
        refresh_irq();
    };
    if (offset >= kPROC0_INTE && offset < kPROC0_INTE + kIntGroups * 4u) group_write(kPROC0_INTE, inte_[0]);
    else if (offset >= kPROC0_INTF && offset < kPROC0_INTF + kIntGroups * 4u) group_write(kPROC0_INTF, intf_[0]);
    else if (offset >= kPROC1_INTE && offset < kPROC1_INTE + kIntGroups * 4u) group_write(kPROC1_INTE, inte_[1]);
    else if (offset >= kPROC1_INTF && offset < kPROC1_INTF + kIntGroups * 4u) group_write(kPROC1_INTF, intf_[1]);
    return BusStatus::Ok;
}

}  // namespace rp2040
