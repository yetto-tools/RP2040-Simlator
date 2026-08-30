// iobank0.h - IO_BANK0 (datasheet 2.19.6.1): per-GPIO function select plus the
// per-pin interrupt block (level/edge detect -> IO_IRQ_BANK0, IRQ 13).
//
// Each GPIO has 8 bytes of config (GPIOx_STATUS, GPIOx_CTRL) and 4 interrupt
// bits (LEVEL_LOW, LEVEL_HIGH, EDGE_LOW, EDGE_HIGH). INTR is the raw status
// (edge bits are write-1-to-clear, level bits track live); PROC0_* and
// PROC1_* are the independent enable/force/status sets routed to core 0 and
// core 1 respectively.
#ifndef RP2040_PERIPHERALS_IOBANK0_H
#define RP2040_PERIPHERALS_IOBANK0_H

#include <array>
#include <cstdint>

#include "core/atomic_peripheral.h"
#include "core/bus.h"
#include "core/cpu.h"
#include "core/memory.h"
#include "exceptions.h"
#include "peripherals/gpio.h"

namespace rp2040 {

class IoBank0 : public AtomicPeripheral {
public:
    static constexpr std::uint32_t kBase = 0x40014000u;
    static constexpr std::uint32_t kSize = AtomicPeripheral::kAtomicSize;
    static constexpr unsigned kIrqBank0 = kExcExternal0 + 13;  // IO_IRQ_BANK0
    static constexpr unsigned kIntGroups = 4;                  // INTR0..3 (8 pins each)

    explicit IoBank0(Gpio& gpio) : gpio_(gpio) {}
    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }

    // Route the bank interrupt to the two cores (PROC0 -> core0, PROC1 -> core1).
    void connect_cores(Cpu* core0, Cpu* core1) { core_[0] = core0; core_[1] = core1; }

    // Sample every pin, update level/edge status, and (de)assert IO_IRQ_BANK0
    // on each core. Call once per Simulator::step().
    void poll();

    BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth w) override;
    BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth w) override;

private:
    void refresh_irq();

    Gpio& gpio_;
    std::array<std::uint32_t, Gpio::kNumPins> ctrl_{};  // last-written GPIOx_CTRL

    std::array<std::uint32_t, kIntGroups> intr_{};      // raw (edge bits sticky)
    std::array<std::array<std::uint32_t, kIntGroups>, 2> inte_{};
    std::array<std::array<std::uint32_t, kIntGroups>, 2> intf_{};
    std::array<Cpu*, 2> core_{};
    std::uint32_t prev_level_ = 0;   // per-pin input level at the last poll
    bool primed_ = false;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_IOBANK0_H
