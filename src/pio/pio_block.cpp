#include "pio/pio_block.h"

namespace rp2040 {

PioBlock::PioBlock(Gpio& gpio, int index) : gpio_(gpio), index_(index) {
    const Gpio::Driver drv = (index == 0) ? Gpio::kPio0 : Gpio::kPio1;
    for (unsigned i = 0; i < kNumSm; ++i) {
        sm_[i].set_program(program_.data());
        sm_[i].set_gpio(&gpio_, drv);
        sm_[i].set_block_irq(&irq_);
        sm_[i].set_sm_id(i);
        clkdiv_[i] = 1u * 256u;   // reset: divide by 1
        clkacc_[i] = 0;
    }
}

void PioBlock::set_irq(unsigned n, bool v) {
    if (n >= 8) return;
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << n);
    if (v) irq_ = static_cast<std::uint8_t>(irq_ | bit);
    else   irq_ = static_cast<std::uint8_t>(irq_ & ~bit);
}

void PioBlock::set_clkdiv(unsigned sm, std::uint16_t int_part, std::uint8_t frac) {
    if (sm >= kNumSm) return;
    const std::uint32_t whole = (int_part == 0) ? 65536u : int_part;
    clkdiv_[sm] = whole * 256u + frac;
    clkacc_[sm] = 0;
}

void PioBlock::tick() {
    for (unsigned i = 0; i < kNumSm; ++i) {
        if (!sm_[i].enabled()) continue;
        // Accumulate 256 units per system clock; step the SM each time the
        // accumulator covers one full divider period.
        clkacc_[i] += 256u;
        if (clkacc_[i] >= clkdiv_[i]) {
            clkacc_[i] -= clkdiv_[i];
            sm_[i].tick();
        }
    }
}

}  // namespace rp2040
