// interrupt_controller.h - fans a peripheral interrupt line out to every CPU
// core (BACKLOG P1.6).
//
// On the RP2040 each peripheral IRQ is wired to *both* Cortex-M0+ cores; each
// core's NVIC independently decides whether to take it (per-core enable +
// priority). Peripherals hold one of these (constructed from their primary
// core) instead of a bare `Cpu&`, and the Simulator adds the second core.
#ifndef RP2040_CORE_INTERRUPT_CONTROLLER_H
#define RP2040_CORE_INTERRUPT_CONTROLLER_H

#include <array>
#include <cstddef>

#include "core/cpu.h"

namespace rp2040 {

class InterruptController {
public:
    InterruptController() = default;
    // Implicit on purpose: a peripheral that used to take `Cpu& cpu` can pass
    // it straight through as the primary core.
    InterruptController(Cpu& primary) { connect(&primary); }  // NOLINT

    void connect(Cpu* core) {
        if (core == nullptr) return;
        for (std::size_t i = 0; i < count_; ++i) {
            if (cores_[i] == core) return;  // idempotent
        }
        if (count_ < cores_.size()) cores_[count_++] = core;
    }

    void pend_exception(unsigned exc) {
        for (std::size_t i = 0; i < count_; ++i) cores_[i]->pend_exception(exc);
    }
    void clear_pending(unsigned exc) {
        for (std::size_t i = 0; i < count_; ++i) cores_[i]->clear_pending(exc);
    }

    std::size_t core_count() const { return count_; }

private:
    std::array<Cpu*, 2> cores_{};
    std::size_t count_ = 0;
};

}  // namespace rp2040

#endif  // RP2040_CORE_INTERRUPT_CONTROLLER_H
