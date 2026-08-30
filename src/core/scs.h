// scs.h - System Control Space peripheral: SysTick, NVIC, SCB (BACKLOG P1.4).
//
// Occupies 0xE000E000-0xE000EFFF on the Private Peripheral Bus. Register
// accesses translate into calls on the owning Cpu (pend/clear exceptions,
// set priorities, IRQ enables, VTOR). SysTick's down-counter is advanced by
// Cpu::step() through on_cycles().
//
// Reference: ARMv6-M ARM sections B3.2 (SCB), B3.3 (SysTick), B3.4 (NVIC).
#ifndef RP2040_CORE_SCS_H
#define RP2040_CORE_SCS_H

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

    explicit Scs(Cpu& cpu) : cpu_(cpu) {}

    // Register this peripheral on the bus and wire it to the CPU.
    bool attach(Memory& mem) {
        cpu_.set_scs(this);
        return mem.attach_peripheral(kBase, kSize, this);
    }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;

    // Advance the SysTick counter by `cycles` core clocks.
    void on_cycles(std::uint64_t cycles);

    // Invoked when firmware writes AIRCR.SYSRESETREQ. If unset, the owning
    // Cpu is reset directly.
    void on_system_reset(std::function<void()> cb) { system_reset_cb_ = std::move(cb); }

    // Test/inspection helpers.
    std::uint32_t systick_cvr() const { return syst_cvr_ & 0xFFFFFFu; }
    bool systick_countflag() const { return (syst_csr_ & (1u << 16)) != 0; }

private:
    std::uint32_t read_ipr(unsigned word) const;
    void write_ipr(unsigned word, std::uint32_t value);
    std::uint32_t read_shpr2() const;
    std::uint32_t read_shpr3() const;

    Cpu& cpu_;

    std::uint32_t syst_csr_ = 0;   // ENABLE|TICKINT|CLKSOURCE|COUNTFLAG
    std::uint32_t syst_rvr_ = 0;   // 24-bit reload
    std::uint32_t syst_cvr_ = 0;   // 24-bit current
    std::uint32_t scr_ = 0;        // SLEEPONEXIT / SLEEPDEEP / SEVONPEND
    std::function<void()> system_reset_cb_;
};

}  // namespace rp2040

#endif  // RP2040_CORE_SCS_H
