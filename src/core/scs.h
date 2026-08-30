// scs.h - System Control Space peripheral: SysTick, NVIC, SCB (BACKLOG P1.4).
//
// Occupies 0xE000E000-0xE000EFFF on the Private Peripheral Bus. On the RP2040
// each core has its *own* SCS at this address; this one object models both,
// switching banks with set_active_core() (the Simulator selects the core that
// is about to run, exactly as it does for the SIO). Register accesses and the
// SysTick counter act on the active core.
//
// Reference: ARMv6-M ARM sections B3.2 (SCB), B3.3 (SysTick), B3.4 (NVIC).
#ifndef RP2040_CORE_SCS_H
#define RP2040_CORE_SCS_H

#include <array>
#include <cstdint>
#include <functional>

#include "core/bus.h"
#include "core/cpu.h"

namespace rp2040 {

class Scs : public BusPeripheral {
public:
    static constexpr std::uint32_t kBase = 0xE000E000u;
    static constexpr std::uint32_t kSize = 0x1000u;

    // Cortex-M0+ r0p1 as used on the RP2040 (datasheet 2.4.8.1).
    static constexpr std::uint32_t kCpuid = 0x410CC601u;

    explicit Scs(Cpu& cpu) { core_[0] = &cpu; }

    // Register this peripheral on the bus and wire it to core 0.
    bool attach(Memory& mem) {
        core_[0]->set_scs(this);
        return mem.attach_peripheral(kBase, kSize, this);
    }

    // Wire core 1 so it has its own SCS bank (SysTick, NVIC enables, ...).
    void connect_core1(Cpu* core1) {
        core_[1] = core1;
        if (core1 != nullptr) core1->set_scs(this);
    }

    // Select which core's bank register accesses and on_cycles() act on.
    void set_active_core(unsigned core) { active_ = (core == 1 && core_[1]) ? 1u : 0u; }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;

    // Advance the active core's SysTick counter by `cycles` core clocks.
    void on_cycles(std::uint64_t cycles);

    // Invoked when firmware writes AIRCR.SYSRESETREQ. If unset, the owning
    // Cpu is reset directly.
    void on_system_reset(std::function<void()> cb) { system_reset_cb_ = std::move(cb); }

    // Test/inspection helpers (active core).
    std::uint32_t systick_cvr() const { return syst_cvr_[active_] & 0xFFFFFFu; }
    bool systick_countflag() const { return (syst_csr_[active_] & (1u << 16)) != 0; }

private:
    Cpu& cur() const { return *core_[active_]; }

    std::uint32_t read_ipr(unsigned word) const;
    void write_ipr(unsigned word, std::uint32_t value);
    std::uint32_t read_shpr2() const;
    std::uint32_t read_shpr3() const;

    std::array<Cpu*, 2> core_{};
    unsigned active_ = 0;

    std::array<std::uint32_t, 2> syst_csr_{};   // ENABLE|TICKINT|CLKSOURCE|COUNTFLAG
    std::array<std::uint32_t, 2> syst_rvr_{};   // 24-bit reload
    std::array<std::uint32_t, 2> syst_cvr_{};   // 24-bit current
    std::array<std::uint32_t, 2> scr_{};        // SLEEPONEXIT / SLEEPDEEP / SEVONPEND
    std::function<void()> system_reset_cb_;
};

}  // namespace rp2040

#endif  // RP2040_CORE_SCS_H
